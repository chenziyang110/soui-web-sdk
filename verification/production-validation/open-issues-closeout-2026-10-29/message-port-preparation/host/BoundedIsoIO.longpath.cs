using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.ComTypes;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;
using Microsoft.Win32.SafeHandles;

namespace SouiDataIso4ddce37c {
    public sealed class FilePin {
        public string path { get; set; }
        public long bytes { get; set; }
        public string sha256 { get; set; }
        public uint volumeSerial { get; set; }
        public string fileIndex { get; set; }
        public uint links { get; set; }
    }
    internal static class Native {
        [StructLayout(LayoutKind.Sequential)] internal struct Info {
            internal uint Attributes;
            internal System.Runtime.InteropServices.ComTypes.FILETIME Created, Accessed, Written;
            internal uint Volume, SizeHigh, SizeLow, Links, IndexHigh, IndexLow;
        }
        [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] internal static extern SafeFileHandle CreateFileW(string name,uint access,uint share,IntPtr security,uint disposition,uint flags,IntPtr template);
        [DllImport("kernel32.dll", SetLastError=true)] internal static extern bool GetFileInformationByHandle(SafeFileHandle handle,out Info info);
        [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] internal static extern uint GetFinalPathNameByHandleW(SafeFileHandle handle,StringBuilder path,uint size,uint flags);
        [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] internal static extern bool CreateDirectoryW(string name,IntPtr security);
        internal static Exception Error(string code) { return new IOException(code, new Win32Exception(Marshal.GetLastWin32Error())); }
    }
    public sealed class ReadLease : IDisposable {
        private SafeFileHandle handle;
        private FileStream stream;
        private Native.Info original;
        private readonly bool directory;
        public string PathName { get; private set; }
        public ReadLease(string path,bool isDirectory) {
            IsoIO.Plain(path); PathName=System.IO.Path.GetFullPath(path); directory=isDirectory;
            try {
                handle=Native.CreateFileW(@"\\?\"+PathName,isDirectory?0x80u:0x80000080u,1,IntPtr.Zero,3,0x00200000u|(isDirectory?0x02000000u:0x08000000u),IntPtr.Zero);
                if(handle.IsInvalid) throw Native.Error("E_OPEN_HELD");
                original=State();
                if(!isDirectory) stream=new FileStream(handle,FileAccess.Read,65536,false);
            } catch { Dispose(); throw; }
        }
        private Native.Info State() {
            Native.Info info;
            if(handle==null||handle.IsClosed||!Native.GetFileInformationByHandle(handle,out info)) throw Native.Error("E_HANDLE_INFO");
            if((info.Attributes&0x400)!=0 || ((info.Attributes&0x10)!=0)!=directory || (!directory&&info.Links!=1)) throw new IOException("E_REPARSE_KIND_OR_HARDLINK");
            var value=new StringBuilder(2048); uint n=Native.GetFinalPathNameByHandleW(handle,value,2048,0);
            if(n==0||n>=2048) throw Native.Error("E_FINAL_PATH");
            string actual=value.ToString();
            if(!actual.StartsWith(@"\\?\",StringComparison.Ordinal)||actual.StartsWith(@"\\?\UNC\",StringComparison.OrdinalIgnoreCase)) throw new IOException("E_FINAL_PATH_KIND");
            actual=actual.Substring(4);
            if(!String.Equals(actual.TrimEnd('\\'),PathName.TrimEnd('\\'),StringComparison.OrdinalIgnoreCase)) throw new IOException("E_FINAL_PATH_ALIAS");
            return info;
        }
        public void CheckIdentity() {
            Native.Info now=State();
            if(now.Volume!=original.Volume||now.IndexHigh!=original.IndexHigh||now.IndexLow!=original.IndexLow||now.SizeHigh!=original.SizeHigh||now.SizeLow!=original.SizeLow||now.Written.dwHighDateTime!=original.Written.dwHighDateTime||now.Written.dwLowDateTime!=original.Written.dwLowDateTime) throw new IOException("E_HELD_IDENTITY_CHANGED");
        }
        public FilePin Hash(long expectedBytes,string expectedSha) {
            if(directory||stream==null||expectedBytes<0||expectedBytes>1300000000) throw new IOException("E_HASH_SHAPE");
            CheckIdentity();
            if(stream.Length!=expectedBytes) throw new IOException("E_SOURCE_LENGTH");
            stream.Position=0; long done=0; byte[] buffer=new byte[65536]; string digest;
            using(var hash=IncrementalHash.CreateHash(HashAlgorithmName.SHA256)) {
                while(done<expectedBytes) {int n=stream.Read(buffer,0,(int)Math.Min(65536L,expectedBytes-done));if(n<=0)throw new IOException("E_SOURCE_EOF");hash.AppendData(buffer,0,n);done+=n;}
                if(stream.ReadByte()!=-1||stream.Length!=expectedBytes)throw new IOException("E_SOURCE_EXTRA");
                digest=Convert.ToHexString(hash.GetHashAndReset()).ToLowerInvariant();
            }
            CheckIdentity();
            if(!String.Equals(digest,expectedSha,StringComparison.Ordinal))throw new IOException("E_SOURCE_SHA");
            return new FilePin{path=PathName,bytes=done,sha256=digest,volumeSerial=original.Volume,fileIndex=original.IndexHigh.ToString("x8")+original.IndexLow.ToString("x8"),links=original.Links};
        }
        public void CopyNew(string destination,long expectedBytes) {
            IsoIO.Plain(destination);CheckIdentity();
            if(directory||stream==null||expectedBytes<0||expectedBytes>1300000000||stream.Length!=expectedBytes)throw new IOException("E_COPY_SOURCE");
            stream.Position=0;byte[] buffer=new byte[65536];long done=0;
            using(var output=new FileStream(destination,FileMode.CreateNew,FileAccess.Write,FileShare.None,65536,FileOptions.SequentialScan)) {
                while(done<expectedBytes){int n=stream.Read(buffer,0,(int)Math.Min(65536L,expectedBytes-done));if(n<=0)throw new IOException("E_COPY_EOF");output.Write(buffer,0,n);done+=n;}
                if(stream.ReadByte()!=-1||stream.Length!=expectedBytes||output.Length!=expectedBytes)throw new IOException("E_COPY_EXTRA");
                output.Flush(true);
            }
            CheckIdentity();
        }
        public void Dispose() {
            if(stream!=null){stream.Dispose();stream=null;handle=null;}
            else if(handle!=null){handle.Dispose();handle=null;}
        }
    }
    public static class IsoIO {
        // Receive four live scalar references. Finish managed identity deduplication
        // before releasing any RCW; return only ordinary strings to PowerShell.
        public static string[] ReleaseOwners(object image, object result, object root, object fsi) {
            object[] candidates = { image, result, root, fsi };
            object[] owners = new object[4];
            string[] errors = new string[4];
            int ownerCount = 0, errorCount = 0;
            for (int i = 0; i < candidates.Length; ++i) {
                object candidate = candidates[i];
                if (Object.ReferenceEquals(candidate, null)) continue;
                bool seen = false;
                for (int j = 0; j < ownerCount; ++j)
                    if (Object.ReferenceEquals(owners[j], candidate)) seen = true;
                if (!seen) owners[ownerCount++] = candidate;
            }
            for (int i = 0; i < ownerCount; ++i) {
                try {
                    if (Marshal.IsComObject(owners[i]))
                        Marshal.FinalReleaseComObject(owners[i]);
                } catch (Exception error) {
                    errors[errorCount++] = error.GetType().FullName;
                }
            }
            string[] resultErrors = new string[errorCount];
            Array.Copy(errors, resultErrors, errorCount);
            return resultErrors;
        }
        public static void Plain(string path) {
            if(path==null||path.Length<3||path.Length>1024||!Regex.IsMatch(path,@"^[A-Za-z]:\\"))throw new IOException("E_LOCAL_PATH");
            string rest=path.Substring(3);
            if(rest.Length!=0)foreach(string part in rest.Split('\\'))if(part.Length==0||part=="."||part==".."||Regex.IsMatch(part,@"[\x00-\x1f<>:""|?*%~/]")||part.EndsWith(".",StringComparison.Ordinal)||part.EndsWith(" ",StringComparison.Ordinal))throw new IOException("E_PATH_COMPONENT");
            for(string at=path;!String.IsNullOrEmpty(at);at=System.IO.Path.GetDirectoryName(at)) {
                try {if((File.GetAttributes(at)&FileAttributes.ReparsePoint)!=0)throw new IOException("E_ANCESTOR_REPARSE");}
                catch(FileNotFoundException) {} catch(DirectoryNotFoundException) {}
            }
        }
        public static void NewDirectory(string path) {
            string parent=System.IO.Path.GetDirectoryName(path);Plain(parent);
            if(!Directory.Exists(parent)||File.Exists(path)||Directory.Exists(path))throw new IOException("E_DIRECTORY_EXISTS_OR_PARENT");
            if(!Native.CreateDirectoryW(path,IntPtr.Zero))throw Native.Error("E_CREATE_DIRECTORY");
            Plain(path);
        }
        public static FilePin CopyImage(object raw,string path,long cap) {
            Plain(System.IO.Path.GetDirectoryName(path));
            if(cap!=1400000000||raw==null)throw new IOException("E_IMAGE_CAP");
            var input=(IStream)raw;System.Runtime.InteropServices.ComTypes.STATSTG stat;input.Stat(out stat,1);long length=stat.cbSize;
            if(length<=0||length>cap||length%2048!=0)throw new IOException("E_IMAGE_LENGTH");
            input.Seek(0,0,IntPtr.Zero);IntPtr count=Marshal.AllocHGlobal(4);
            try {
                using(var output=new FileStream(path,FileMode.CreateNew,FileAccess.ReadWrite,FileShare.None,65536,FileOptions.SequentialScan)) {
                    byte[] buffer=new byte[65536];long done=0;string writeHash;
                    using(var hash=IncrementalHash.CreateHash(HashAlgorithmName.SHA256)) {
                        while(done<length){int want=(int)Math.Min(65536L,length-done);Marshal.WriteInt32(count,0);input.Read(buffer,want,count);int n=Marshal.ReadInt32(count);if(n<=0||n>want)throw new IOException("E_IMAGE_SHORT");output.Write(buffer,0,n);hash.AppendData(buffer,0,n);done+=n;}
                        Marshal.WriteInt32(count,0);input.Read(buffer,1,count);if(Marshal.ReadInt32(count)!=0)throw new IOException("E_IMAGE_EXTRA");
                        writeHash=Convert.ToHexString(hash.GetHashAndReset()).ToLowerInvariant();
                    }
                    input.Stat(out stat,1);if(stat.cbSize!=length||output.Length!=length||output.Position!=length)throw new IOException("E_IMAGE_CHANGED");
                    output.Flush(true);output.Position=0;long checkedBytes=0;string actualHash;
                    using(var hash=IncrementalHash.CreateHash(HashAlgorithmName.SHA256)) {
                        while(checkedBytes<length){int n=output.Read(buffer,0,(int)Math.Min(65536L,length-checkedBytes));if(n<=0)throw new IOException("E_ISO_READBACK_EOF");hash.AppendData(buffer,0,n);checkedBytes+=n;}
                        if(output.ReadByte()!=-1||output.Length!=length)throw new IOException("E_ISO_READBACK_EXTRA");actualHash=Convert.ToHexString(hash.GetHashAndReset()).ToLowerInvariant();
                    }
                    if(actualHash!=writeHash)throw new IOException("E_ISO_READBACK_SHA");
                    Native.Info info;if(!Native.GetFileInformationByHandle(output.SafeFileHandle,out info)||info.Links!=1||(info.Attributes&0x410)!=0)throw new IOException("E_ISO_PHYSICAL_INFO");
                    return new FilePin{path=System.IO.Path.GetFullPath(path),bytes=length,sha256=actualHash,volumeSerial=info.Volume,fileIndex=info.IndexHigh.ToString("x8")+info.IndexLow.ToString("x8"),links=info.Links};
                }
            } finally {Marshal.FreeHGlobal(count);}
        }
    }
}
