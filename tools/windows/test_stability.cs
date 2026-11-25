using System;
using System.IO;
using System.Threading;
using System.Diagnostics;
using ReDUB;

class StabilityTests {
    static void Check(bool ok,string name){if(!ok)throw new Exception(name);Console.WriteLine("PASS: "+name);}
    static void Put(string path,string text){Directory.CreateDirectory(Path.GetDirectoryName(path));File.WriteAllText(path,text);}
    [STAThread]static void Main(string[] args){
        if(args[0]=="--hold"){Console.WriteLine("READY");Console.ReadLine();return;}
        string fixture=Path.GetFullPath(args[0]);
        if(Directory.Exists(fixture))throw new Exception("Use a new fixture directory.");
        string root=Path.Combine(fixture,"install"),source=Path.Combine(fixture,"disc");
        string game=Path.Combine(root,"game-remix"),save=Path.Combine(game,@"uDaTa\54540079\profile\career.mc3");
        Put(Path.Combine(source,"default.xbe"),"valid fixture");
        Put(Path.Combine(source,@"UDATA\54540079\profile\career.mc3"),"incoming profile");
        Put(save,"installed career");Put(Path.Combine(game,@"TDATA\settings.bin"),"installed settings");
        Launcher.ImportGameFiles(root,source,true,null,CancellationToken.None);
        Check(File.ReadAllText(save)=="installed career","replacement preserves installed career over incoming profiles");
        Check(File.ReadAllText(Path.Combine(game,@"TDATA\settings.bin"))=="installed settings","replacement preserves title settings");
        Check(Directory.GetDirectories(game).Length==2,"replacement does not create case-duplicate UDATA");
        using(var locked=new FileStream(save,FileMode.Open,FileAccess.Read,FileShare.None)){
            bool failed=false;try{Launcher.ImportGameFiles(root,source,true,null,CancellationToken.None);}catch(IOException){failed=true;}
            Check(failed,"unreadable save aborts import");
        }
        Check(File.ReadAllText(save)=="installed career","failed save copy leaves original installed");
        var cancellation=new CancellationTokenSource();cancellation.Cancel();
        try{Launcher.ImportGameFiles(root,source,true,null,cancellation.Token);throw new Exception("Cancelled import succeeded");}catch(OperationCanceledException){}
        Check(File.ReadAllText(save)=="installed career","cancelled import leaves original installed");
        Put(Path.Combine(source,"default.xbe"),"invalid");
        try{Launcher.ImportGameFiles(root,source,true,null,CancellationToken.None);throw new Exception("Invalid import succeeded");}catch(IOException){}
        Check(File.ReadAllText(save)=="installed career","failed validation leaves original installed");
        string running=Path.Combine(fixture,"mcdub3remix.exe");
        File.Copy(System.Reflection.Assembly.GetExecutingAssembly().Location,running);
        using(var process=Process.Start(new ProcessStartInfo(running,"--hold"){UseShellExecute=false,CreateNoWindow=true,RedirectStandardInput=true,RedirectStandardOutput=true})){
            try{
                Check(process.StandardOutput.ReadLine()=="READY","running-game fixture started");
                bool refused=false;try{Launcher.ImportGameFiles(root,source,true,null,CancellationToken.None);}catch(IOException e){refused=e.Message.Contains("close the game");}
                Check(refused,"CLI importer refuses a running game");
            }finally{process.StandardInput.WriteLine();process.WaitForExit();}
        }
        string dist=Launcher.RendererDist(root),settings=Path.Combine(dist,"mcdub3remix_env.txt");
        Put(Path.Combine(dist,"mcdub3remix_defaults.txt"),"RECOMP_GL_SCALE=2\n");
        Put(Path.Combine(dist,"mcdub3remix_env.example.txt"),"# Preserve this comment\n");
        using(var launcher=new Launcher(root)){
            Check(launcher.ReadSetting("RECOMP_GL_SCALE","5")=="2","fresh settings read build defaults");
            launcher.SavePreference("RECOMP_GL_SCALE","1.5");
            Check(launcher.ReadSetting("RECOMP_GL_SCALE","5")=="1.5","fresh installation creates settings atomically");
            File.AppendAllText(settings,"RECOMP_GL_SCALE = \"3\"\nUNRELATED=yes\n");
            Check(launcher.ReadSetting("RECOMP_GL_SCALE","5")=="3","quoted settings use last matching key");
            launcher.SavePreference("RECOMP_GL_SCALE","1");
            Check(launcher.ReadSetting("RECOMP_GL_SCALE","5")=="1","saving replaces spaced duplicate keys");
            Check(File.ReadAllText(settings).Contains("UNRELATED=yes")&&File.ReadAllText(settings).Contains("# Preserve this comment"),"saving retains unrelated settings and comments");
            Check(File.Exists(settings+".launcher-backup"),"existing settings retain initial backup");
        }
    }
}

namespace ReDUB {
    // These tests exercise the production importer without copyrighted disc
    // data. Only its validator/extractor dependency is replaced.
    public static class Xdvdfs {
        public static string Verify(string path){if(File.ReadAllText(Path.Combine(path,"default.xbe"))!="valid fixture")throw new IOException("Invalid fixture");return "fixture";}
        public static bool TryVerify(string path,out string edition,out string error){try{edition=Verify(path);error=null;return true;}catch(IOException e){edition=null;error=e.Message;return false;}}
        public static void Extract(string source,string destination,Action<long,long,string> progress,CancellationToken token){throw new NotSupportedException();}
    }
}
