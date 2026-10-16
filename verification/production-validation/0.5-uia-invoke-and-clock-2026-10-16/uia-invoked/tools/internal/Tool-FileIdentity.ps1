function Assert-ToolFileIdentity {
 param([Parameter(Mandatory)][string]$ObservedPath,
       [Parameter(Mandatory)][object]$ExpectedPin)
 $ErrorActionPreference='Stop'
 if($ExpectedPin.bytes -le 0 -or $ExpectedPin.sha256 -cnotmatch '^[0-9a-f]{64}$'){
  throw 'Invalid expected tool pin'
 }
 foreach($path in @($ObservedPath,[string]$ExpectedPin.path)){
  if($path -notmatch '^[a-zA-Z]:[\\/]') {throw 'Absolute local tool path required'}
  $full=[IO.Path]::GetFullPath($path)
  $leaf=Get-Item -LiteralPath $full -ErrorAction Stop
  if($leaf.PSIsContainer -or ($leaf.Attributes -band [IO.FileAttributes]::ReparsePoint)){
   throw 'Plain tool file required'
  }
  $ancestor=[IO.Path]::GetDirectoryName($full)
  while($ancestor){
   $item=Get-Item -LiteralPath $ancestor -ErrorAction Stop
   if(-not $item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)){
    throw 'Plain tool ancestor required'
   }
   $next=[IO.Path]::GetDirectoryName($ancestor)
   if($next -eq $ancestor){break}
   $ancestor=$next
  }
 }
 if(-not ('Soui.Experimental.ToolFileIdentityV1' -as [type])){
  Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using Microsoft.Win32.SafeHandles;
namespace Soui.Experimental {
 public sealed class ToolIdentityReceiptV1 {
  public string ObservedPath {get;set;}
  public string ExpectedPath {get;set;}
  public string FinalPath {get;set;}
  public string VolumeSerial {get;set;}
  public string FileIndex {get;set;}
  public long Bytes {get;set;}
  public string Sha256 {get;set;}
 }
 public static class ToolFileIdentityV1 {
  [StructLayout(LayoutKind.Sequential)]
  private struct Info {
   public uint Attributes, CreationLow, CreationHigh, AccessLow, AccessHigh,
    WriteLow, WriteHigh, VolumeSerial, SizeHigh, SizeLow, Links, IndexHigh, IndexLow;
  }
  [DllImport("kernel32.dll", SetLastError=true)]
  [return:MarshalAs(UnmanagedType.Bool)]
  private static extern bool GetFileInformationByHandle(SafeFileHandle handle,out Info info);
  [DllImport("kernel32.dll", CharSet=CharSet.Unicode, ExactSpelling=true, SetLastError=true)]
  private static extern uint GetFinalPathNameByHandleW(SafeFileHandle handle,StringBuilder path,uint size,uint flags);
  private static Info ReadInfo(FileStream stream) {
   Info info;
   if(!GetFileInformationByHandle(stream.SafeFileHandle,out info))
    throw new Win32Exception(Marshal.GetLastWin32Error());
   if((info.Attributes & 0x410u)!=0 || (info.IndexHigh==0 && info.IndexLow==0))
    throw new IOException("Plain file with usable identity required");
   return info;
  }
  private static string FinalPath(FileStream stream) {
   var text=new StringBuilder(512);
   uint size=GetFinalPathNameByHandleW(stream.SafeFileHandle,text,(uint)text.Capacity,0);
   if(size==0) throw new Win32Exception(Marshal.GetLastWin32Error());
   if(size>32768) throw new IOException("Final tool path extent exceeded");
   if(size>=text.Capacity) {
    text=new StringBuilder(checked((int)size+1));
    size=GetFinalPathNameByHandleW(stream.SafeFileHandle,text,(uint)text.Capacity,0);
    if(size==0) throw new Win32Exception(Marshal.GetLastWin32Error());
    if(size>=text.Capacity) throw new IOException("Final tool path changed");
   }
   return text.ToString();
  }
  private static long Length(Info info) {
   return checked((long)(((ulong)info.SizeHigh<<32)|info.SizeLow));
  }
  private static bool SameIdentity(Info a,Info b) {
   return a.VolumeSerial==b.VolumeSerial && a.IndexHigh==b.IndexHigh && a.IndexLow==b.IndexLow;
  }
  private static string Hash(FileStream stream) {
   stream.Position=0;
   using(var sha=SHA256.Create()) {
    return BitConverter.ToString(sha.ComputeHash(stream)).Replace("-","").ToLowerInvariant();
   }
  }
  public static ToolIdentityReceiptV1 Check(string observed,string expected,long bytes,string sha256) {
   // Both handles deny write/delete sharing until all identity and body checks finish.
   using(var wanted=new FileStream(expected,FileMode.Open,FileAccess.Read,FileShare.Read))
   using(var actual=new FileStream(observed,FileMode.Open,FileAccess.Read,FileShare.Read)) {
    var w=ReadInfo(wanted); var a=ReadInfo(actual);
    string wf=FinalPath(wanted),af=FinalPath(actual);
    if(!SameIdentity(w,a) || !String.Equals(wf,af,StringComparison.OrdinalIgnoreCase))
     throw new IOException("Observed tool is a different physical file");
    if(Length(w)!=bytes || Length(a)!=bytes || wanted.Length!=bytes || actual.Length!=bytes)
     throw new IOException("Observed tool length differs from frozen pin");
    string wh=Hash(wanted),ah=Hash(actual);
    if(wh!=sha256 || ah!=sha256) throw new IOException("Observed tool body differs from frozen pin");
    var wa=ReadInfo(wanted);var aa=ReadInfo(actual);
    if(!SameIdentity(w,wa) || !SameIdentity(a,aa) || Length(wa)!=bytes || Length(aa)!=bytes ||
       wa.WriteLow!=w.WriteLow || wa.WriteHigh!=w.WriteHigh || aa.WriteLow!=a.WriteLow || aa.WriteHigh!=a.WriteHigh)
     throw new IOException("Observed tool changed during identity checks");
    return new ToolIdentityReceiptV1 {ObservedPath=observed,ExpectedPath=expected,FinalPath=af,
     VolumeSerial=a.VolumeSerial.ToString("x8"),FileIndex=a.IndexHigh.ToString("x8")+a.IndexLow.ToString("x8"),
     Bytes=bytes,Sha256=ah};
   }
  }
 }
}
'@
 }
 [Soui.Experimental.ToolFileIdentityV1]::Check($ObservedPath,[string]$ExpectedPin.path,
  [long]$ExpectedPin.bytes,[string]$ExpectedPin.sha256)
}
