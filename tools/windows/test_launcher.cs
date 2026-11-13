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
        Check(Launcher.Renderer(fixture)=="opengl","Default renderer changed.");
        Launcher.SetBypass(fixture,true);Launcher.SetRenderer(fixture,"vulkan");
        Check(Launcher.BypassEnabled(fixture)&&Launcher.Renderer(fixture)=="vulkan","Renderer/bypass preferences conflict.");
        Check(Launcher.LaunchInfo(fixture).Arguments.Contains("Start-Vulkan-Experiment.ps1"),"Vulkan launch routing failed.");
        Check(!Launcher.InstallationReady(fixture),"Missing renderer accepted as installed.");
        string vk=Path.Combine(fixture,Launcher.RendererDist(fixture));Directory.CreateDirectory(vk);Directory.CreateDirectory(Path.Combine(fixture,"game-remix"));
        foreach(string file in new[]{"mcdub3remix.exe","SDL2.dll","redub_dlss.dll","nvngx_dlss.dll"})File.WriteAllText(Path.Combine(vk,file),"fixture");
        File.WriteAllText(Path.Combine(fixture,@"game-remix\default.xbe"),"fixture");
        Launcher.SetDlssMode(fixture,"Quality");
        Check(Launcher.InstallationReady(fixture),"Complete Vulkan + DLSS runtime was rejected.");
        Check(Launcher.LaunchInfo(fixture).Arguments.Contains("-DlssMode Quality")&&Launcher.LaunchInfo(fixture).Arguments.Contains("-PresentScene"),"DLSS Quality launch routing failed.");
        foreach(string mode in new[]{"Balanced","Performance"}){
            Launcher.SetDlssMode(fixture,mode);
            Check(Launcher.DlssMode(fixture)==mode&&Launcher.LaunchInfo(fixture).Arguments.Contains("-DlssMode "+mode)&&Launcher.LaunchInfo(fixture).Arguments.Contains("-PresentScene"),"DLSS mode routing failed: "+mode);
        }
        File.Delete(Path.Combine(vk,"nvngx_dlss.dll"));Check(!Launcher.InstallationReady(fixture),"DLSS Quality accepted a missing NVIDIA runtime.");
        Launcher.SetDlssMode(fixture,"Off");Check(Launcher.InstallationReady(fixture),"Native Vulkan was blocked by a missing optional DLSS runtime.");
        Check(Launcher.LaunchInfo(fixture).Arguments.Contains("-DlssMode Off")&&!Launcher.LaunchInfo(fixture).Arguments.Contains("-PresentScene"),"DLSS Off did not route to native Vulkan.");
        Launcher.SetDlssMode(fixture,"Quality");Launcher.SetRenderer(fixture,"opengl");
        string gl=Path.Combine(fixture,Launcher.RendererDist(fixture));Directory.CreateDirectory(gl);
        foreach(string file in new[]{"mcdub3remix.exe","SDL2.dll"})File.WriteAllText(Path.Combine(gl,file),"fixture");
        Check(Launcher.InstallationReady(fixture)&&Launcher.LaunchInfo(fixture).FileName.EndsWith(@"build-windows-x86_64\dist\mcdub3remix.exe"),"OpenGL rollback was blocked by the saved DLSS preference.");
        Launcher.SetRenderer(fixture,"opengl");
        Check(!Launcher.ShouldBypass(true,new[]{"--launcher"},false,true),"Recovery override failed.");
        bool rejected=false;try{Launcher.SetRenderer(fixture,"fake-dlss");}catch(ArgumentException){rejected=true;}
        Check(rejected,"Unsupported renderer accepted.");
        Console.WriteLine("PASS: moved launcher artwork, local-folder bootstrap, complete embedded source, safe extraction, data exclusions, OpenGL/Vulkan routing, reversible DLSS runtime checks, settings preservation and recovery override.");
    }
}
