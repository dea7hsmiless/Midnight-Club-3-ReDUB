using System;
using System.IO;
using System.IO.Compression;
using System.Reflection;
using ReDUB;

class LauncherTests {
    static void Check(bool ok,string message){if(!ok)throw new Exception(message);}
    [STAThread]static void Main(string[] args){
        string original=Path.GetFullPath(args[0]).TrimEnd('\\');
        Check(!Directory.Exists(Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"launcher","art")),"Test must run without loose artwork.");
        using(var logo=BrandPanel.LoadArt("logo.png"))Check(logo!=null&&logo.Width>100,"Embedded logo failed.");
        using(var hero=BrandPanel.LoadArt("hero.jpg"))Check(hero!=null&&hero.Width>100,"Embedded background failed.");
        Check(Path.GetFullPath(Launcher.ResolveRoot()).TrimEnd('\\')==AppDomain.CurrentDomain.BaseDirectory.TrimEnd('\\'),"Moved launcher redirected builds away from its own folder.");
        Check(Launcher.IsSourceRoot(AppDomain.CurrentDomain.BaseDirectory),"Moved launcher did not restore setup source beside itself.");
        string fixture=Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"extracted");
        Launcher.ExtractEmbeddedSource(fixture);
        Check(Launcher.IsSourceRoot(fixture),"Embedded setup source missing.");
        Check(File.Exists(Path.Combine(fixture,@"xboxrecomp\src\kernel\kernel_path.c")),"Runtime source missing.");
        Check(File.Exists(Path.Combine(fixture,@"tools\regen.sh")),"Code conversion setup missing.");
        Check(!Directory.Exists(Path.Combine(fixture,"game-remix"))&&!Directory.Exists(Path.Combine(fixture,"work-remix")),"Source bundle contains local game/build data.");
        foreach(var path in Directory.GetFiles(fixture,"*",SearchOption.AllDirectories)){
            string ext=Path.GetExtension(path).ToLowerInvariant();
            Check(ext!=".xbe"&&ext!=".iso"&&ext!=".dll"&&ext!=".exe"&&ext!=".keys","Forbidden source bundle file: "+path);
        }
        string sentinel=Path.Combine(fixture,@".portable\Build.ps1");File.WriteAllText(sentinel,"player-owned sentinel");
        Launcher.ExtractEmbeddedSource(fixture);Check(File.ReadAllText(sentinel)=="player-owned sentinel","Bootstrap overwrote an existing file.");
        Check(!Launcher.BypassEnabled(fixture),"Bypass should default off.");
        Launcher.SetBypass(fixture,true);
        Check(Launcher.BypassEnabled(fixture),"Bypass preference did not persist.");
        Check(!Launcher.InstallationReady(fixture),"Missing renderer accepted as installed.");
        string gl=Launcher.RendererDist(fixture);Directory.CreateDirectory(gl);Directory.CreateDirectory(Path.Combine(fixture,"game-remix"));
        foreach(string file in new[]{"mcdub3remix.exe","SDL2.dll"})File.WriteAllText(Path.Combine(gl,file),"fixture");
        File.WriteAllText(Path.Combine(fixture,@"game-remix\default.xbe"),"fixture");
        Check(Launcher.InstallationReady(fixture),"Complete OpenGL runtime was rejected.");
        Check(Launcher.LaunchInfo(fixture).FileName.EndsWith(@"build-windows-x86_64\dist\mcdub3remix.exe"),"Launch does not use the OpenGL dist.");
        Check(!Launcher.LaunchInfo(fixture).Arguments.Contains("-DlssMode"),"Launch still routes through DLSS.");
        Console.WriteLine("PASS: moved launcher artwork, local-folder bootstrap, complete embedded source, safe extraction, data exclusions, OpenGL routing, bypass preference, settings preservation and recovery override.");
    }
}
