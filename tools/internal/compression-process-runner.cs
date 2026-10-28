using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Web.Script.Serialization;

// Compile once before measurement. The supervisor stays outside the measured
// Job; the real executable is assigned while suspended, before its first thread.
public static class SouiCompressionProcessRunner
{
    [StructLayout(LayoutKind.Sequential)] struct SecurityAttributes { public int Length; public IntPtr Descriptor; [MarshalAs(UnmanagedType.Bool)] public bool Inherit; }
    [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)] struct StartupInfo {
        public int Size; public string Reserved, Desktop, Title;
        public uint X,Y,XSize,YSize,XCount,YCount,Fill,Flags;
        public ushort Show, ReservedSize; public IntPtr ReservedBytes, Input, Output, Error;
    }
    [StructLayout(LayoutKind.Sequential)] struct ProcessInfo { public IntPtr Process, Thread; public uint ProcessId, ThreadId; }
    [StructLayout(LayoutKind.Sequential)] struct BasicLimits {
        public long ProcessTime, JobTime; public uint Flags;
        public UIntPtr MinimumWorkingSet, MaximumWorkingSet; public uint ActiveProcessLimit;
        public UIntPtr Affinity; public uint Priority, Scheduling;
    }
    [StructLayout(LayoutKind.Sequential)] struct IoCounters { public ulong ReadOperations, WriteOperations, OtherOperations, ReadBytes, WriteBytes, OtherBytes; }
    [StructLayout(LayoutKind.Sequential)] struct ExtendedLimits {
        public BasicLimits Basic; public IoCounters Io;
        public UIntPtr ProcessLimit, JobLimit, PeakProcessMemory, PeakJobMemory;
    }
    [StructLayout(LayoutKind.Sequential)] struct Accounting {
        public long UserTime, KernelTime, PeriodUserTime, PeriodKernelTime;
        public uint PageFaults, TotalProcesses, ActiveProcesses, TerminatedProcesses;
    }
    [StructLayout(LayoutKind.Sequential)] struct MemoryCounters {
        public uint Size, PageFaults;
        public UIntPtr PeakWorkingSet, WorkingSet, PeakPagedPool, PagedPool, PeakNonPagedPool, NonPagedPool, Pagefile, PeakPagefile, PrivateUsage;
    }
    [DllImport("kernel32.dll", SetLastError=true, CharSet=CharSet.Unicode)] static extern IntPtr CreateJobObjectW(IntPtr attributes, string name);
    [DllImport("kernel32.dll", SetLastError=true)] static extern bool SetInformationJobObject(IntPtr job, int kind, ref ExtendedLimits value, uint bytes);
    [DllImport("kernel32.dll", SetLastError=true)] static extern bool QueryInformationJobObject(IntPtr job, int kind, ref ExtendedLimits value, uint bytes, IntPtr returned);
    [DllImport("kernel32.dll", SetLastError=true)] static extern bool QueryInformationJobObject(IntPtr job, int kind, ref Accounting value, uint bytes, IntPtr returned);
    [DllImport("kernel32.dll", SetLastError=true)] static extern bool QueryInformationJobObject(IntPtr job, int kind, IntPtr value, uint bytes, IntPtr returned);
    [DllImport("kernel32.dll", SetLastError=true)] static extern bool AssignProcessToJobObject(IntPtr job, IntPtr process);
    [DllImport("kernel32.dll", SetLastError=true)] static extern bool TerminateJobObject(IntPtr job, uint exitCode);
    [DllImport("kernel32.dll", SetLastError=true)] static extern bool TerminateProcess(IntPtr process, uint exitCode);
    [DllImport("kernel32.dll", SetLastError=true)] static extern uint ResumeThread(IntPtr thread);
    [DllImport("kernel32.dll", SetLastError=true)] static extern uint WaitForSingleObject(IntPtr handle, uint milliseconds);
    [DllImport("kernel32.dll", SetLastError=true)] static extern bool GetExitCodeProcess(IntPtr process, out uint exitCode);
    [DllImport("kernel32.dll", SetLastError=true)] static extern bool CloseHandle(IntPtr handle);
    [DllImport("kernel32.dll", SetLastError=true)] static extern IntPtr OpenProcess(uint access, bool inherit, uint processId);
    [DllImport("kernel32.dll", SetLastError=true, CharSet=CharSet.Unicode)] static extern bool QueryFullProcessImageNameW(IntPtr process, uint flags, StringBuilder image, ref uint characters);
    [DllImport("kernel32.dll", SetLastError=true, CharSet=CharSet.Unicode)] static extern bool CreateProcessW(string application, StringBuilder commandLine, IntPtr processAttributes, IntPtr threadAttributes, bool inherit, uint flags, IntPtr environment, string directory, ref StartupInfo startup, out ProcessInfo process);
    [DllImport("kernel32.dll", SetLastError=true, CharSet=CharSet.Unicode)] static extern IntPtr CreateFileW(string file, uint access, uint share, ref SecurityAttributes attributes, uint disposition, uint flags, IntPtr template);
    [DllImport("psapi.dll", SetLastError=true)] static extern bool GetProcessMemoryInfo(IntPtr process, ref MemoryCounters counters, uint bytes);

    public sealed class Request { public string Executable; public string[] Arguments; public string Directory; public string Log; public int TimeoutMs; }
    static void Need(bool condition, string operation) { if (!condition) throw new Win32Exception(Marshal.GetLastWin32Error(), operation); }
    static string Quote(string value) {
        if (value == null) throw new ArgumentException("Null command argument");
        var output = new StringBuilder("\""); int slashes = 0;
        foreach (char character in value) {
            if (character == '\\') { slashes++; continue; }
            if (character == '"') output.Append('\\', slashes * 2 + 1).Append('"');
            else output.Append('\\', slashes).Append(character);
            slashes = 0;
        }
        return output.Append('\\', slashes * 2).Append('"').ToString();
    }
    static uint Active(IntPtr job) {
        var accounting = new Accounting();
        Need(QueryInformationJobObject(job, 1, ref accounting, (uint)Marshal.SizeOf(typeof(Accounting)), IntPtr.Zero), "Query job accounting");
        return accounting.ActiveProcesses;
    }
    static void Observe(IntPtr job, HashSet<uint> seen, List<object> observations) {
        const int size=65536;
        IntPtr buffer=Marshal.AllocHGlobal(size);
        try {
            Need(QueryInformationJobObject(job,3,buffer,size,IntPtr.Zero),"Query owned job process identities");
            int count=Marshal.ReadInt32(buffer,4);
            if(count<0 || count>(size-8)/IntPtr.Size) throw new InvalidOperationException("Invalid owned job process count");
            for(int index=0;index<count;index++) {
                uint pid=(uint)Marshal.ReadIntPtr(buffer,8+index*IntPtr.Size).ToInt64();
                if(!seen.Add(pid)) continue;
                IntPtr handle=OpenProcess(0x1000,false,pid); string imagePath=null;
                int error=handle==IntPtr.Zero?Marshal.GetLastWin32Error():0;
                if(handle!=IntPtr.Zero) {
                    try {
                        var image=new StringBuilder(32768);uint length=(uint)image.Capacity;
                        if(QueryFullProcessImageNameW(handle,0,image,ref length)) imagePath=image.ToString();
                        else error=Marshal.GetLastWin32Error();
                    } finally { CloseHandle(handle); }
                }
                observations.Add(new {processId=pid,imagePath=imagePath,queryError=error});
            }
        } finally { Marshal.FreeHGlobal(buffer); }
    }
    public static object Run(Request request) {
        if (request == null || !Path.IsPathRooted(request.Executable) || !File.Exists(request.Executable) ||
            !Path.IsPathRooted(request.Directory) || !Directory.Exists(request.Directory) ||
            !Path.IsPathRooted(request.Log) || request.TimeoutMs < 1 || request.Arguments == null)
            throw new ArgumentException("Invalid process measurement request");
        IntPtr job=IntPtr.Zero, output=IntPtr.Zero, input=IntPtr.Zero;
        ProcessInfo process = new ProcessInfo(); bool assigned=false;
        try {
            var attributes=new SecurityAttributes { Length=Marshal.SizeOf(typeof(SecurityAttributes)), Inherit=true };
            output=CreateFileW(request.Log,0x40000000,3,ref attributes,1,0x80,IntPtr.Zero);
            Need(output!=new IntPtr(-1),"Create unique output log");
            input=CreateFileW("NUL",0x80000000,3,ref attributes,3,0x80,IntPtr.Zero);
            Need(input!=new IntPtr(-1),"Open child input");
            job=CreateJobObjectW(IntPtr.Zero,null); Need(job!=IntPtr.Zero,"Create owned job");
            var limits=new ExtendedLimits(); limits.Basic.Flags=0x2000; // KILL_ON_JOB_CLOSE
            Need(SetInformationJobObject(job,9,ref limits,(uint)Marshal.SizeOf(typeof(ExtendedLimits))),"Set owned job limits");
            var startup=new StartupInfo { Size=Marshal.SizeOf(typeof(StartupInfo)), Flags=0x101, Show=0, Input=input,Output=output,Error=output };
            var command=new StringBuilder(Quote(request.Executable));
            foreach(string argument in request.Arguments) command.Append(' ').Append(Quote(argument));
            Need(CreateProcessW(request.Executable,command,IntPtr.Zero,IntPtr.Zero,true,0x08000004,IntPtr.Zero,request.Directory,ref startup,out process),"Create suspended process");
            Need(AssignProcessToJobObject(job,process.Process),"Assign suspended process to owned job"); assigned=true;
            var observed=new List<object>();var seen=new HashSet<uint>();Observe(job,seen,observed);
            var startedAt=DateTime.UtcNow; var stopwatch=Stopwatch.StartNew();
            Need(ResumeThread(process.Thread)!=0xffffffff,"Resume owned process");
            bool timedOut=false;
            while(Active(job)!=0) {
                Observe(job,seen,observed);
                if(stopwatch.ElapsedMilliseconds>=request.TimeoutMs) {
                    timedOut=true; Need(TerminateJobObject(job,1460),"Stop timed-out owned job"); break;
                }
                WaitForSingleObject(process.Process,5);
                if(WaitForSingleObject(process.Process,0)==0) Thread.Sleep(1);
            }
            if(timedOut) {
                var cleanup=Stopwatch.StartNew();
                while(Active(job)!=0 && cleanup.ElapsedMilliseconds<5000) Thread.Sleep(5);
                if(Active(job)!=0) throw new InvalidOperationException("Timed-out owned job did not drain");
            }
            stopwatch.Stop();
            Need(WaitForSingleObject(process.Process,5000)==0,"Wait for owned process exit");
            uint exitCode; Need(GetExitCodeProcess(process.Process,out exitCode),"Read process exit");
            limits=new ExtendedLimits();
            Need(QueryInformationJobObject(job,9,ref limits,(uint)Marshal.SizeOf(typeof(ExtendedLimits)),IntPtr.Zero),"Read complete job memory peaks");
            var accounting=new Accounting();
            Need(QueryInformationJobObject(job,1,ref accounting,(uint)Marshal.SizeOf(typeof(Accounting)),IntPtr.Zero),"Read complete job process count");
            if(limits.PeakProcessMemory.ToUInt64()==0 || limits.PeakJobMemory.ToUInt64()==0)
                throw new InvalidOperationException("Windows did not return job memory peaks");
            var counters=new MemoryCounters { Size=(uint)Marshal.SizeOf(typeof(MemoryCounters)) };
            bool direct=GetProcessMemoryInfo(process.Process,ref counters,counters.Size);
            int directError=direct?0:Marshal.GetLastWin32Error();
            var directCounters=new Dictionary<string,object> { {"available",direct && counters.PeakWorkingSet.ToUInt64()>0 && counters.PeakPagefile.ToUInt64()>0}, {"win32Error",directError},
                {"scope","Primary process lifetime peaks queried from retained process handle after exit; zero/unavailable values are not measurements"} };
            if(direct && counters.PeakWorkingSet.ToUInt64()>0 && counters.PeakPagefile.ToUInt64()>0) {
                directCounters.Add("peakWorkingSetBytes",counters.PeakWorkingSet.ToUInt64());
                directCounters.Add("peakPrivateCommitBytes",counters.PeakPagefile.ToUInt64());
            }
            return new { schema="soui.windows-process-measurement/v1", executable=request.Executable, arguments=request.Arguments,
                directory=request.Directory,log=request.Log,startedAt=startedAt.ToString("o"),elapsedMs=stopwatch.Elapsed.TotalMilliseconds,
                exitCode=exitCode,timedOut=timedOut,jobDrained=true,
                totalProcesses=accounting.TotalProcesses,observedProcesses=observed,elapsedPollingIntervalMs=5,
                elapsedScope="Immediately before ResumeThread through observed Job drain; excludes supervisor startup, suspended process creation, assignment, compilation and hashing; includes observer/identity-query overhead and 1-5ms drain polling granularity",
                memory=new { metric="private-commit",method="QueryInformationJobObject(JobObjectExtendedLimitInformation)",
                    scope="Whole owned Job lifetime; executable created suspended, assigned before resume; supervisor excluded",
                    peakProcessMemoryBytes=limits.PeakProcessMemory.ToUInt64(),peakJobMemoryBytes=limits.PeakJobMemory.ToUInt64(),processCounters=directCounters } };
        } finally {
            if(process.Process!=IntPtr.Zero && !assigned) { TerminateProcess(process.Process,1460); WaitForSingleObject(process.Process,5000); }
            if(job!=IntPtr.Zero) CloseHandle(job);
            if(process.Thread!=IntPtr.Zero) CloseHandle(process.Thread);
            if(process.Process!=IntPtr.Zero) CloseHandle(process.Process);
            if(input!=IntPtr.Zero && input!=new IntPtr(-1)) CloseHandle(input);
            if(output!=IntPtr.Zero && output!=new IntPtr(-1)) CloseHandle(output);
        }
    }
    public static int Main(string[] args) {
        try {
            Console.OutputEncoding=new UTF8Encoding(false);
            Console.SetError(new StreamWriter(Console.OpenStandardError(),new UTF8Encoding(false)) { AutoFlush=true });
            if(args.Length==3 && args[0]=="allocate") {
                int bytes=int.Parse(args[1]),delay=int.Parse(args[2]);
                var data=new byte[bytes]; for(int index=0;index<data.Length;index+=4096) data[index]=1;
                if(bytes>0) data[bytes-1]=1;
                Console.WriteLine("allocated-and-touched-bytes="+bytes); Thread.Sleep(delay); GC.KeepAlive(data); return 0;
            }
            if(args.Length!=2 || args[0]!="run") throw new ArgumentException("Expected run REQUEST or allocate BYTES DELAY");
            var json=new JavaScriptSerializer { MaxJsonLength=1024*1024,RecursionLimit=32 };
            Console.WriteLine(json.Serialize(Run(json.Deserialize<Request>(File.ReadAllText(args[1],Encoding.UTF8))))); return 0;
        } catch(Exception error) { Console.Error.WriteLine(error.ToString()); return 1; }
    }
}
