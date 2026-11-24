using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading;
using System.Windows.Forms;
using System.Reflection;
using System.IO.Compression;
[assembly: AssemblyTitle("ReDUB Launcher")]
[assembly: AssemblyDescription("Midnight Club 3: DUB Edition Remix setup, build and play launcher")]
[assembly: AssemblyProduct("ReDUB")]
[assembly: AssemblyCompany("ReDUB")]
[assembly: AssemblyCopyright("Fan project; Midnight Club 3 is a trademark of its owners")]
[assembly: AssemblyVersion("0.2.4.0")]
[assembly: AssemblyFileVersion("0.2.4.0")]
namespace ReDUB {
public class RedubButton : Control, IButtonControl {
    public bool Primary, Selected;
    sealed class ButtonAccessibility : ControlAccessibleObject {
        readonly RedubButton owner;
        public ButtonAccessibility(RedubButton button):base(button){owner=button;}
        public override AccessibleRole Role {get{return AccessibleRole.PushButton;}}
        public override string DefaultAction {get{return "Press";}}
        public override void DoDefaultAction(){owner.PerformClick();}
    }
    protected override AccessibleObject CreateAccessibilityInstance(){return new ButtonAccessibility(this);}
    bool pressed;
    public DialogResult DialogResult {get;set;}
    public void NotifyDefault(bool value){Invalidate();}
    public void PerformClick(){if(Enabled){OnClick(EventArgs.Empty);if(DialogResult!=DialogResult.None&&FindForm()!=null)FindForm().DialogResult=DialogResult;}}
    public RedubButton(){SetStyle(ControlStyles.UserPaint|ControlStyles.AllPaintingInWmPaint|ControlStyles.OptimizedDoubleBuffer|ControlStyles.ResizeRedraw|ControlStyles.Opaque|ControlStyles.Selectable|ControlStyles.StandardClick,true);Cursor=Cursors.Hand;Font=new Font("Segoe UI",10,FontStyle.Bold);Height=42;ForeColor=Color.FromArgb(225,230,241);BackColor=Color.FromArgb(17,25,44);AccessibleRole=AccessibleRole.PushButton;TabStop=true;}
    protected override bool IsInputKey(Keys key){return (key&Keys.KeyCode)==Keys.Space||(key&Keys.KeyCode)==Keys.Enter||base.IsInputKey(key);}
    protected override void OnKeyDown(KeyEventArgs e){if(e.KeyCode==Keys.Space||e.KeyCode==Keys.Enter){pressed=true;e.Handled=true;Invalidate();}base.OnKeyDown(e);}
    protected override void OnKeyUp(KeyEventArgs e){if((e.KeyCode==Keys.Space||e.KeyCode==Keys.Enter)&&pressed){pressed=false;e.Handled=true;PerformClick();Invalidate();}base.OnKeyUp(e);}
    protected override void OnMouseDown(MouseEventArgs e){if(e.Button==MouseButtons.Left){Focus();pressed=true;Invalidate();}base.OnMouseDown(e);}
    protected override void OnMouseUp(MouseEventArgs e){pressed=false;Invalidate();base.OnMouseUp(e);}
    protected override void OnGotFocus(EventArgs e){base.OnGotFocus(e);Invalidate();}
    protected override void OnLostFocus(EventArgs e){pressed=false;base.OnLostFocus(e);Invalidate();}
    protected override void OnTextChanged(EventArgs e){base.OnTextChanged(e);AccessibleName=Text;Invalidate();}
    // Own the complete surface, including rounded corners and text. Avoid
    // native Button theme painting underneath our custom control.
    protected override void OnPaintBackground(PaintEventArgs e){e.Graphics.Clear(Parent!=null?Parent.BackColor:BackColor);}
    protected override void OnMouseEnter(EventArgs e){base.OnMouseEnter(e);Invalidate();}
    protected override void OnMouseLeave(EventArgs e){base.OnMouseLeave(e);Invalidate();}
    protected override void OnPaint(PaintEventArgs e){var g=e.Graphics;g.Clear(Parent!=null?Parent.BackColor:BackColor);g.SmoothingMode=SmoothingMode.AntiAlias;var r=new Rectangle(1,1,Width-3,Height-3);using(var p=Round(r,8)){Color bg=Primary?Color.FromArgb(238,62,80):Selected?Color.FromArgb(30,44,72):BackColor;if(!Enabled)bg=Color.FromArgb(36,41,56);else if(pressed)bg=ControlPaint.Dark(bg,.12f);else if(ClientRectangle.Contains(PointToClient(MousePosition)))bg=ControlPaint.Light(bg,.12f);using(var b=new SolidBrush(bg))g.FillPath(b,p);using(var pen=new Pen(Selected?Color.FromArgb(238,62,80):Color.FromArgb(48,61,87)))g.DrawPath(pen,p);}if(Selected)using(var b=new SolidBrush(Color.FromArgb(245,80,96)))g.FillRectangle(b,1,9,3,Height-18);TextRenderer.DrawText(g,Text,Font,ClientRectangle,Enabled?ForeColor:Color.FromArgb(132,141,160),TextFormatFlags.HorizontalCenter|TextFormatFlags.VerticalCenter|TextFormatFlags.NoPrefix|TextFormatFlags.SingleLine|TextFormatFlags.EndEllipsis);if(Focused)ControlPaint.DrawFocusRectangle(g,new Rectangle(5,5,Width-11,Height-11));}
    public static GraphicsPath Round(Rectangle r,int n){var p=new GraphicsPath();int d=n*2;p.AddArc(r.X,r.Y,d,d,180,90);p.AddArc(r.Right-d,r.Y,d,d,270,90);p.AddArc(r.Right-d,r.Bottom-d,d,d,0,90);p.AddArc(r.X,r.Bottom-d,d,d,90,90);p.CloseFigure();return p;}
}
public class BrandPanel : Panel {
    Image hero,logo;
    public static Image LoadArt(string name){try{string path=Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"launcher","art",name);using(var stream=File.Exists(path)?new MemoryStream(File.ReadAllBytes(path)):Assembly.GetExecutingAssembly().GetManifestResourceStream("ReDUB.Art."+name)){if(stream==null)return null;using(var img=Image.FromStream(stream))return new Bitmap(img);}}catch{return null;}}
    public BrandPanel(){DoubleBuffered=true;hero=LoadArt("hero.jpg")??LoadArt("hero.png");logo=LoadArt("logo.png");}
    protected override void Dispose(bool disposing){if(disposing){if(hero!=null)hero.Dispose();if(logo!=null)logo.Dispose();}base.Dispose(disposing);}
    protected override void OnPaint(PaintEventArgs e){base.OnPaint(e);var g=e.Graphics;g.TextRenderingHint=System.Drawing.Text.TextRenderingHint.AntiAliasGridFit;g.SmoothingMode=SmoothingMode.AntiAlias;using(var bg=new LinearGradientBrush(ClientRectangle,Color.FromArgb(31,21,38),Color.FromArgb(8,13,26),75))g.FillRectangle(bg,ClientRectangle);if(hero!=null){float zoom=Math.Max((float)Width/hero.Width,(float)Height/hero.Height);float w=hero.Width*zoom,h=hero.Height*zoom;g.InterpolationMode=InterpolationMode.HighQualityBicubic;g.DrawImage(hero,(Width-w)/2,(Height-h)/2,w,h);using(var wash=new LinearGradientBrush(ClientRectangle,Color.FromArgb(80,8,13,26),Color.FromArgb(225,8,13,26),90))g.FillRectangle(wash,ClientRectangle);}var ellipse=new Rectangle(Width/2-300,Height/2-140,600,280);using(var p=new GraphicsPath()){p.AddEllipse(ellipse);using(var glow=new PathGradientBrush(p)){glow.CenterColor=Color.FromArgb(135,93,27,51);glow.SurroundColors=new[]{Color.FromArgb(0,8,13,26)};g.FillPath(glow,p);}}if(logo!=null){float zoom=Math.Min((float)Width*.78f/logo.Width,(float)Height*.65f/logo.Height);float w=logo.Width*zoom,h=logo.Height*zoom;g.InterpolationMode=InterpolationMode.HighQualityBicubic;g.DrawImage(logo,(Width-w)/2,(Height-h)/2,w,h);return;}float size=Math.Min(132,Width/5.5f);using(var f=new Font("Segoe UI",size,FontStyle.Bold|FontStyle.Italic)){var measured=g.MeasureString("ReDUB",f);float x=(Width-measured.Width)/2;float y=(Height-measured.Height)/2+8;using(var shadow=new SolidBrush(Color.FromArgb(120,0,0,0)))g.DrawString("ReDUB",f,shadow,x+4,y+6);using(var ink=new SolidBrush(Color.FromArgb(244,246,252)))g.DrawString("ReDUB",f,ink,x,y);using(var red=new SolidBrush(Color.FromArgb(242,65,84)))g.FillRectangle(red,x+16,y+measured.Height-20,measured.Width-20,4);using(var sub=new Font("Segoe UI",12,FontStyle.Bold))using(var ink=new SolidBrush(Color.FromArgb(188,196,213)))g.DrawString("M I D N I G H T   C L U B   3",sub,ink,(Width-280)/2,y-4);}}
}
public sealed class Launcher : Form {
    readonly string root;readonly Label status=new Label();readonly TextBox source=new TextBox();readonly RichTextBox log=new RichTextBox();readonly RedubButton play=new RedubButton {Primary=true};readonly ProgressBar progress=new ProgressBar();readonly System.Windows.Forms.Timer refresh=new System.Windows.Forms.Timer();readonly Panel content=new Panel();readonly List<RedubButton> navigation=new List<RedubButton>();Process job;string page="Game";string category="Display";bool advancedWheelSettings,advancedGraphicsSettings;volatile bool working;string importStatus="";CancellationTokenSource importCancellation;RedubButton cancelImport,importAction;bool closeAfterImport;
    static readonly Color Bg=Color.FromArgb(8,13,26),Rail=Color.FromArgb(5,9,18),Card=Color.FromArgb(15,23,42),Muted=Color.FromArgb(153,165,188);
    public Launcher(string directory){root=directory;Text="Midnight Club 3: ReDUB";Size=new Size(1200,800);MinimumSize=new Size(1000,700);StartPosition=FormStartPosition.CenterScreen;BackColor=Bg;ForeColor=Color.FromArgb(235,239,247);Font=new Font("Segoe UI",10);AutoScaleMode=AutoScaleMode.Dpi;KeyPreview=true;KeyDown+=(s,e)=>{if(working&&e.KeyCode==Keys.Escape){CancelImport();e.Handled=true;}};
        var shell=new TableLayoutPanel {Dock=DockStyle.Fill,ColumnCount=2,RowCount=2,Margin=Padding.Empty,Padding=Padding.Empty};shell.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute,220));shell.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,100));shell.RowStyles.Add(new RowStyle(SizeType.Percent,100));shell.RowStyles.Add(new RowStyle(SizeType.Absolute,38));Controls.Add(shell);
        var rail=new Panel {Dock=DockStyle.Fill,BackColor=Rail,Margin=Padding.Empty,Padding=new Padding(16)};shell.Controls.Add(rail,0,0);var mark=BrandPanel.LoadArt("logo.png");
        if(mark!=null){var brand=new PictureBox {Image=mark,SizeMode=PictureBoxSizeMode.Zoom,BackColor=Rail,AccessibleName="Midnight Club 3 ReDUB logo",TabStop=false};brand.SetBounds(16,24,188,84);brand.Disposed+=(sender,e)=>mark.Dispose();rail.Controls.Add(brand);}
        else{var brand=LabelAt("ReDUB",25,true);brand.SetBounds(16,30,188,50);brand.ForeColor=Color.FromArgb(245,80,96);rail.Controls.Add(brand);}
        Nav(rail,"Game",16,118,()=>ShowHome());Nav(rail,"Install",16,180,()=>ShowInstall());Nav(rail,"Settings",16,242,()=>ShowSettings(category));
        var bottom=new Panel {Dock=DockStyle.Bottom,Height=120};rail.Controls.Add(bottom);Nav(bottom,"Support",0,4,()=>ShowSupport());Nav(bottom,"About",0,62,()=>ShowAbout());
        content.Dock=DockStyle.Fill;content.Margin=Padding.Empty;shell.Controls.Add(content,1,0);
        var footer=new Panel {Dock=DockStyle.Fill,BackColor=Color.FromArgb(14,22,39),Margin=Padding.Empty};shell.Controls.Add(footer,0,1);shell.SetColumnSpan(footer,2);status.SetBounds(20,9,930,22);status.Anchor=AnchorStyles.Left|AnchorStyles.Top|AnchorStyles.Right;status.ForeColor=Muted;status.Font=new Font("Segoe UI",9);footer.Controls.Add(status);progress.SetBounds(980,12,165,14);progress.Anchor=AnchorStyles.Right|AnchorStyles.Top;progress.Style=ProgressBarStyle.Marquee;progress.Visible=false;footer.Controls.Add(progress);
        source.BackColor=Card;source.ForeColor=ForeColor;source.BorderStyle=BorderStyle.FixedSingle;source.Font=Font;log.ReadOnly=true;log.BackColor=Rail;log.ForeColor=ForeColor;log.BorderStyle=BorderStyle.None;log.Font=new Font("Consolas",9);
        play.Click+=(s,e)=>{if(Ready())PlayGame();else ShowInstall();};Append("ReDUB launcher ready. Setup, checks and build output appear here.");Append("Installation folder: "+root);ShowHome();refresh.Interval=1500;refresh.Tick+=(s,e)=>UpdateStatus();refresh.Start();UpdateStatus();FormClosing+=(s,e)=>{
            if(job!=null&&!job.HasExited){MessageBox.Show(this,"Setup is still running. Leave ReDUB open until it finishes.","Setup in progress");e.Cancel=true;return;}
            if(!working)return;
            if(closeAfterImport){e.Cancel=true;return;}
            if(MessageBox.Show(this,"An import is running. Cancel it and exit?\n\nThe previous game files are left untouched.","Import in progress",MessageBoxButtons.YesNo,MessageBoxIcon.Warning)!=DialogResult.Yes){e.Cancel=true;return;}
            closeAfterImport=true;e.Cancel=true;CancelImport();
        };
    }
    static Label LabelAt(string text,float size,bool bold){return new Label {Text=text,Font=new Font("Segoe UI",size,bold?FontStyle.Bold:FontStyle.Regular),ForeColor=Color.FromArgb(230,235,245),BackColor=Color.Transparent,AutoEllipsis=true};}
    void Nav(Control host,string title,int x,int y,Action action){var b=new RedubButton {Text=title,Tag=title,BackColor=Rail};b.SetBounds(x,y,188,52);b.Click+=(s,e)=>action();host.Controls.Add(b);navigation.Add(b);}
    void Page(string name){page=name;if(play.Parent!=null)play.Parent.Controls.Remove(play);if(source.Parent!=null)source.Parent.Controls.Remove(source);if(log.Parent!=null)log.Parent.Controls.Remove(log);foreach(Control old in content.Controls.Cast<Control>().ToArray())old.Dispose();content.Controls.Clear();foreach(var b in navigation){b.Selected=(string)b.Tag==name;b.Invalidate();}}
    RedubButton ButtonAt(Control parent,string text,int x,int y,int width,Action action,bool primary=false){var b=new RedubButton {Text=text,Primary=primary};b.SetBounds(x,y,width,42);b.Click+=(s,e)=>{try{action();}catch(Exception ex){MessageBox.Show(this,ex.Message,"ReDUB",MessageBoxButtons.OK,MessageBoxIcon.Error);}};parent.Controls.Add(b);return b;}
    Panel Standard(string name,string title,string help){Page(name);var p=new Panel {Dock=DockStyle.Fill,Padding=new Padding(36),AutoScroll=true};content.Controls.Add(p);var h=LabelAt(title,24,true);h.SetBounds(36,30,800,48);p.Controls.Add(h);var d=LabelAt(help,10,false);d.ForeColor=Muted;d.SetBounds(38,83,790,48);d.Anchor=AnchorStyles.Top|AnchorStyles.Left|AnchorStyles.Right;p.Controls.Add(d);return p;}
    bool Ready(){return InstallationReady(root);}
    bool GameRunning(){var all=Process.GetProcessesByName("mcdub3remix");bool found=all.Length>0;foreach(var p in all)p.Dispose();return found;}
    void ShowHome(){Page("Game");bool ready=Ready(),runtime=RuntimePresent(root);var grid=new TableLayoutPanel {Dock=DockStyle.Fill,ColumnCount=1,RowCount=2,Padding=Padding.Empty,Margin=Padding.Empty};grid.RowStyles.Add(new RowStyle(SizeType.Percent,100));grid.RowStyles.Add(new RowStyle(SizeType.Absolute,240));content.Controls.Add(grid);grid.Controls.Add(new BrandPanel {Dock=DockStyle.Fill,Margin=Padding.Empty},0,0);
        // Separate columns prevent transparent text labels from overlapping
        // the action controls when the window is narrow or DPI is scaled.
        var bottom=new TableLayoutPanel {Dock=DockStyle.Fill,Margin=Padding.Empty,Padding=new Padding(36,0,36,0),ColumnCount=2,RowCount=1};
        bottom.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,100));bottom.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute,296));bottom.RowStyles.Add(new RowStyle(SizeType.Percent,100));grid.Controls.Add(bottom,0,1);
        var left=new Panel {Dock=DockStyle.Fill,Margin=Padding.Empty,BackColor=Bg};bottom.Controls.Add(left,0,0);
        string badgeText=ready?"●  INSTALLED  /  WINDOWS x64":runtime?"○  GAME FILES NEEDED":"○  RUNTIME MISSING";
        var badge=LabelAt(badgeText,9,true);badge.ForeColor=ready?Color.FromArgb(84,210,165):Muted;badge.SetBounds(0,24,360,24);left.Controls.Add(badge);
        var title=LabelAt("Midnight Club 3: ReDUB",23,true);title.SetBounds(0,58,360,48);left.Controls.Add(title);
        var desc=LabelAt("DUB Edition Remix, rebuilt for PC.",10,false);desc.ForeColor=Muted;desc.SetBounds(0,112,360,30);left.Controls.Add(desc);
        Action sizeLabels=()=>{foreach(Control label in left.Controls)label.Width=Math.Max(1,left.ClientSize.Width-16);float fontSize=left.ClientSize.Width<440?20:23;if(Math.Abs(title.Font.Size-fontSize)>.1f){var oldFont=title.Font;title.Font=new Font("Segoe UI",fontSize,FontStyle.Bold);oldFont.Dispose();}};left.SizeChanged+=(sender,e)=>sizeLabels();left.Layout+=(sender,e)=>sizeLabels();sizeLabels();
        var right=new Panel {Dock=DockStyle.Fill,Margin=Padding.Empty,BackColor=Bg};bottom.Controls.Add(right,1,0);play.Text=ready?"▶   Play":"Install";play.SetBounds(0,22,288,58);right.Controls.Add(play);ButtonAt(right,"Verify setup",0,92,140,()=>VerifySetup());ButtonAt(right,"Repair",148,92,140,()=>ShowInstall());ButtonAt(right,"Game folder",0,146,140,()=>Open(RendererDist(root)));ButtonAt(right,"Add shortcut",148,146,140,()=>Run(".portable/Build-Launcher.ps1","-ShortcutsOnly"));UpdateStatus();}
    void ShowInstall(){
        bool ready=Ready(),runtime=RuntimePresent(root);
        var p=Standard("Install","Install ReDUB",ready?"Your original Xbox disc files are installed. Play starts the game directly; nothing is compiled.":"Copy your own original Xbox DUB Edition Remix disc into this folder. Nothing is compiled.");
        var box=new Panel {BackColor=Card,Location=new Point(36,146),Size=new Size(Math.Max(600,content.Width-72),330),Anchor=AnchorStyles.Left|AnchorStyles.Top|AnchorStyles.Right};p.Controls.Add(box);
        var h=LabelAt(ready?"Your game files are ready":runtime?"Choose your game files":"The game runtime is missing",17,true);h.SetBounds(24,20,720,40);box.Controls.Add(h);
        var hint=LabelAt(ready?"The disc files are in game-remix. Replacing them keeps saves and settings.":runtime?"Select an original Xbox Remix ISO or a folder containing default.xbe. PS2 and Switch files are not supported. Press Esc to cancel an import.":"This release folder has no game executable. Download the full ReDUB Windows zip, or rebuild from source in Settings → Advanced. You can still install game files now.",10,false);hint.ForeColor=Muted;hint.SetBounds(24,65,720,54);box.Controls.Add(hint);
        source.SetBounds(24,125,box.Width-48,30);source.Anchor=AnchorStyles.Left|AnchorStyles.Top|AnchorStyles.Right;box.Controls.Add(source);
        ButtonAt(box,"Choose ISO",24,173,164,()=>{using(var d=new OpenFileDialog {Filter="Xbox disc image (*.iso;*.xiso)|*.iso;*.xiso|All files (*.*)|*.*",Title="Choose Xbox Remix disc"})if(d.ShowDialog(this)==DialogResult.OK)source.Text=d.FileName;});
        ButtonAt(box,"Choose folder",200,173,164,()=>{using(var d=new FolderBrowserDialog {Description="Extracted Xbox Remix folder"})if(d.ShowDialog(this)==DialogResult.OK)source.Text=d.SelectedPath;});
        var add=ButtonAt(box,ready?"Replace game files":"Install",24,246,270,()=>StartImport(source.Text),true);add.Enabled=!working;importAction=add;var cancel=ButtonAt(box,"Cancel import",310,246,150,()=>CancelImport());cancel.Visible=working;cancel.Enabled=working;cancelImport=cancel;
        var note=LabelAt("Files are copied into game-remix beside this launcher. No compiler, MSYS2 or download is used.\nUse a writable folder. Save and close the game first.",10,false);note.ForeColor=Muted;note.SetBounds(38,504,800,70);p.Controls.Add(note);
        ButtonAt(p,"Check game files",36,600,210,()=>VerifySetup());
    }
    void ShowSettings(string cat){category=cat;var p=Standard("Settings","Settings",cat=="Startup"?"Startup options apply the next time you open ReDUB.":"Changes are saved for the next launch. Close the game before changing game settings here.");var categories=new[]{"Display","Graphics","Controls","Music","Network","Mods","Diagnostics","Startup","Advanced"};for(int i=0;i<categories.Length;i++){string c=categories[i];var b=ButtonAt(p,c,36,145+i*54,160,()=>ShowSettings(c));b.Selected=c==cat;}
        var card=new Panel {BackColor=Card,Location=new Point(220,145),Size=new Size(Math.Max(510,content.Width-256),Math.Max(420,content.Height-185)),Anchor=AnchorStyles.Top|AnchorStyles.Bottom|AnchorStyles.Left|AnchorStyles.Right,AutoScroll=true};p.Controls.Add(card);var heading=LabelAt(cat,17,true);heading.SetBounds(24,20,480,38);card.Controls.Add(heading);int y=84;
        if(cat=="Display"){Choice(card,ref y,"Window mode","MC3_FULLSCREEN",new[]{"Windowed","Borderless fullscreen","Exclusive fullscreen"},new[]{"0","1","2"},"0");Choice(card,ref y,"Resolution","MC3_RESOLUTION",new[]{"Automatic (desktop)","1280 × 720","1600 × 900","1920 × 1080","2560 × 1440","3840 × 2160"},new[]{"auto","1280x720","1600x900","1920x1080","2560x1440","3840x2160"},"auto");Choice(card,ref y,"Aspect ratio (restart)","RECOMP_GL_ASPECT",new[]{"Auto (16:9)","4:3","Stretch"},new[]{"auto","4:3","fill"},"auto");Choice(card,ref y,"VSync","RECOMP_GL_VSYNC",new[]{"Off","On","Adaptive"},new[]{"0","1","-1"},"0");Choice(card,ref y,"Gamma","MC3_GAMMA",new[]{"0.8","0.9","1.0","1.1","1.2","1.3","1.4"},new[]{"0.8","0.9","1","1.1","1.2","1.3","1.4"},"1");Choice(card,ref y,"Brightness","MC3_BRIGHTNESS",new[]{"80%","85%","90%","95%","100%","105%","110%","115%","120%"},new[]{"0.8","0.85","0.9","0.95","1","1.05","1.1","1.15","1.2"},"1");}
        
        if(cat=="Graphics"){Choice(card,ref y,"Preset","MC3_GRAPHICS_PRESET",new[]{"Custom","Performance","Balanced","Quality"},new[]{"0","1","2","3"},"0");Choice(card,ref y,"Internal resolution","RECOMP_GL_SCALE",new[]{"1× native (720 × 480)","1.5× native (1080 × 720)","2× native (1440 × 960)","3× native (2160 × 1440)","5× native (3600 × 2400)"},new[]{"1","1.5","2","3","5"},"2");Choice(card,ref y,"Frame generation","RECOMP_FG",new[]{"Off","Crossfade","Motion-compensated"},new[]{"0","1","2"},"0");Choice(card,ref y,"Frame rate","MC3_FRAME_RATE",new[]{"30 FPS (Xbox)","60 FPS","120 FPS (needs a 120 Hz display)"},new[]{"30","60","120"},"30");Choice(card,ref y,"HUD texture quality","RECOMP_GL_UI_SCALE",new[]{"1×","2×","3×","5×"},new[]{"1","2","3","5"},"2");Choice(card,ref y,"Anti-aliasing","RECOMP_GL_FXAA",new[]{"Off","FXAA Low","FXAA","FXAA High"},new[]{"0","2","1","3"},"1");Choice(card,ref y,"Texture filtering","RECOMP_GL_ANISO",new[]{"1×","4×","16×"},new[]{"1","4","16"},"16");Choice(card,ref y,"Sharpening","RECOMP_GL_SHARPEN",new[]{"Off","25%","50%","75%","100%"},new[]{"0","0.25","0.5","0.75","1"},"0.5");var scaleHint=LabelAt("Internal resolution multiplies the game's 720 × 480 surface. 3× is 2160 × 1440, 5× is 3600 × 2400 (4K-class detail).",9,false);scaleHint.ForeColor=Muted;scaleHint.SetBounds(24,y+8,Math.Max(400,card.Width-48),40);scaleHint.Anchor=AnchorStyles.Top|AnchorStyles.Left|AnchorStyles.Right;card.Controls.Add(scaleHint);y+=56;}
        if(cat=="Graphics"){
            var advanced=ButtonAt(card,advancedGraphicsSettings?"Hide advanced graphics":"Advanced graphics  ›",24,y,280,()=>{advancedGraphicsSettings=!advancedGraphicsSettings;ShowSettings("Graphics");});y+=58;
            if(advancedGraphicsSettings)Choice(card,ref y,"Frame queue","RECOMP_GL_QUEUE_FRAMES",new[]{"1 frame (lower latency)","2 frames (default)"},new[]{"1","2"},"2");
        }
        if(cat=="Controls"){
            Choice(card,ref y,"Controller vibration","MC3_RUMBLE_STRENGTH",new[]{"Off","50%","75%","100%","125%","150%"},new[]{"0","50","75","100","125","150"},"100");ButtonAt(card,"Test Xbox vibration",24,y,220,()=>Run("tools/windows/Test-ControllerVibration.ps1"));y+=62;
            Choice(card,ref y,"Steering wheel support","MC3_WHEEL",new[]{"Off","On"},new[]{"0","1"},"0");
            if(ReadSetting("MC3_WHEEL","0")=="1"){
                Choice(card,ref y,"Wheel rumble","MC3_WHEEL_RUMBLE",new[]{"Off","50%","75%","100%","125%","150%"},new[]{"0","50","75","100","125","150"},"100");
                var advanced=ButtonAt(card,advancedWheelSettings?"Hide advanced wheel mapping":"Advanced wheel mapping  ›",24,y,280,()=>{advancedWheelSettings=!advancedWheelSettings;ShowSettings("Controls");});y+=58;
                if(advancedWheelSettings){
                    Choice(card,ref y,"Wheel device","MC3_WHEEL_DEVICE",new[]{"Auto detect","Device 0","Device 1","Device 2","Device 3","Device 4","Device 5","Device 6","Device 7"},new[]{"auto","0","1","2","3","4","5","6","7"},"auto");
                    Choice(card,ref y,"Steering axis","MC3_WHEEL_STEER_AXIS",new[]{"Axis 0","Axis 1","Axis 2","Axis 3"},new[]{"0","1","2","3"},"0");
                    Choice(card,ref y,"Throttle axis","MC3_WHEEL_THROTTLE_AXIS",new[]{"Axis 0","Axis 1","Axis 2","Axis 3","Axis 4","Axis 5","Axis 6","Axis 7"},new[]{"0","1","2","3","4","5","6","7"},"1");
                    Choice(card,ref y,"Brake axis","MC3_WHEEL_BRAKE_AXIS",new[]{"Axis 0","Axis 1","Axis 2","Axis 3","Axis 4","Axis 5","Axis 6","Axis 7"},new[]{"0","1","2","3","4","5","6","7"},"2");
                    Choice(card,ref y,"Invert steering","MC3_WHEEL_STEER_INVERT",new[]{"No","Yes"},new[]{"0","1"},"0");
                    Choice(card,ref y,"Invert throttle","MC3_WHEEL_THROTTLE_INVERT",new[]{"No","Yes"},new[]{"0","1"},"0");
                    Choice(card,ref y,"Invert brake","MC3_WHEEL_BRAKE_INVERT",new[]{"No","Yes"},new[]{"0","1"},"0");
                    Choice(card,ref y,"Wheel deadzone","MC3_WHEEL_DEADZONE",new[]{"0%","2%","5%","10%"},new[]{"0","655","1638","3276"},"1638");
                }
            }
            var controlsHint=LabelAt("Xbox controllers use Windows gamepad support. Wheel compatibility depends on the model and its SDL input mapping. Rumble requires device support; full force-feedback steering is not included.",10,false);controlsHint.ForeColor=Muted;controlsHint.SetBounds(24,y+8,Math.Max(400,card.Width-48),64);controlsHint.Anchor=AnchorStyles.Top|AnchorStyles.Left|AnchorStyles.Right;card.Controls.Add(controlsHint);
        }
        if(cat=="Music"){Choice(card,ref y,"PC Music while driving","MC3_PC_MUSIC",new[]{"Off","On"},new[]{"0","1"},"0");Choice(card,ref y,"Song display","MC3_PC_MUSIC_HUD",new[]{"Overlay","Radio + fallback"},new[]{"0","1"},"1");Choice(card,ref y,"Player volume","MC3_PC_MUSIC_VOLUME",new[]{"Mute","10%","20%","30%","40%","50%","60%","70%","80%","90%","100%"},new[]{"0","10","20","30","40","50","60","70","80","90","100"},"100");var info=LabelAt("Uses built-in Windows media controls with supported players/services\nsuch as YouTube Music, Spotify and Pandora. No login is needed in ReDUB.\nMusic plays directly from your PC; ReDUB reads track info and controls\nsupported players. D-pad left/right requests track changes while driving.",10,false);info.ForeColor=Muted;info.SetBounds(24,y+10,Math.Max(400,card.Width-48),130);info.Anchor=AnchorStyles.Top|AnchorStyles.Left|AnchorStyles.Right;card.Controls.Add(info);}
        if(cat=="Network"){
            Choice(card,ref y,"LAN Play (System Link) (restart)","MC3_NETPLAY",new[]{"Off","On"},new[]{"0","1"},"0");
            var netHint=LabelAt("Two or more copies on the same network can then see each other under Networking -> Xbox System Link: Create Match on one, Join Game on the other. Your PC's LAN address is detected automatically; if a firewall asks, allow UDP 20000-21000. Across the internet, use a VPN and set RECOMP_NET_PEERS in mcdub3remix_env.txt to the other machine's VPN address.",10,false);netHint.ForeColor=Muted;netHint.SetBounds(24,y,Math.Max(400,card.Width-48),120);netHint.Anchor=AnchorStyles.Top|AnchorStyles.Left|AnchorStyles.Right;card.Controls.Add(netHint);
        }
        if(cat=="Mods"){
            var modsLabel=LabelAt("Unlock everything (restart)",10,false);modsLabel.SetBounds(24,y,300,38);card.Controls.Add(modsLabel);
            var modsToggle=ButtonAt(card,ReadSetting("MC3_UNLOCK_ALL","0")=="1"?"On":"Off",Math.Max(245,card.Width-270),y-4,244,()=>{});modsToggle.Anchor=AnchorStyles.Right|AnchorStyles.Top;
            modsToggle.Click+=(sender,e)=>{try{if(GameRunning()){MessageBox.Show(this,"Save and close the game first.","Game is running");return;}string next=ReadSetting("MC3_UNLOCK_ALL","0")=="1"?"0":"1";SavePreference("MC3_UNLOCK_ALL",next);modsToggle.Text=next=="1"?"On":"Off";}catch(Exception ex){MessageBox.Show(this,ex.Message,"Could not save settings",MessageBoxButtons.OK,MessageBoxIcon.Error);}};
            y+=62;
            var modsHint=LabelAt("Every car, part, rim, paint and driving ability is available, and every race and city is open. Your career save and money are untouched. Applies on the next launch; a car picks up an unlocked ability at its next build (a cruise load or a garage visit).",10,false);modsHint.ForeColor=Muted;modsHint.SetBounds(24,y,Math.Max(400,card.Width-48),110);modsHint.Anchor=AnchorStyles.Top|AnchorStyles.Left|AnchorStyles.Right;card.Controls.Add(modsHint);y+=118;
            var modsWhat=LabelAt("A mod changes what the game does: its rules, progression or content. Presentation, performance and startup options live under Settings. More mods will appear here, each off by default and restarting to apply.",10,false);modsWhat.ForeColor=Muted;modsWhat.SetBounds(24,y,Math.Max(400,card.Width-48),92);modsWhat.Anchor=AnchorStyles.Top|AnchorStyles.Left|AnchorStyles.Right;card.Controls.Add(modsWhat);
        }
        if(cat=="Diagnostics"){Choice(card,ref y,"FPS / frame time","MC3_FPS_OVERLAY",new[]{"Off","On"},new[]{"0","1"},"0");ButtonAt(card,"Open logs",24,y+12,190,()=>Open("work-remix/logs"));ButtonAt(card,"Check setup",230,y+12,190,()=>VerifySetup());}
        if(cat=="Startup"){
            Choice(card,ref y,"Start in Arcade Cruise (restart)","MC3_AUTOSTART",new[]{"Off","Cruise"},new[]{"0","cruise"},"0");
            Choice(card,ref y,"Skip intro movies (restart)","MC3_SKIP_MOVIES",new[]{"Off","On"},new[]{"0","1"},"0");
            var startupHint=LabelAt("Cruise starts the game and enters Arcade, Cruise on its own, pressing only what the menus allow. Any input from you stops it, and it stops if you have no profile yet. Skipping the intro presses through the boot movies. These change how the game starts, not what it does.",10,false);startupHint.ForeColor=Muted;startupHint.SetBounds(24,y,Math.Max(400,card.Width-48),96);startupHint.Anchor=AnchorStyles.Top|AnchorStyles.Left|AnchorStyles.Right;card.Controls.Add(startupHint);y+=104;
            var label=LabelAt("Skip launcher on startup",10,false);label.SetBounds(24,y,270,38);card.Controls.Add(label);
            var toggle=ButtonAt(card,BypassEnabled(root)?"On":"Off",Math.Max(245,card.Width-270),y-4,244,()=>{});toggle.Anchor=AnchorStyles.Right|AnchorStyles.Top;
            toggle.Click+=(sender,e)=>{try{SetBypass(root,!BypassEnabled(root));toggle.Text=BypassEnabled(root)?"On":"Off";}catch(Exception ex){MessageBox.Show(this,ex.Message,"Could not save launcher setting");}};
            var hint=LabelAt("When enabled, opening ReDUB starts the installed game directly.\n\nHold Shift while opening ReDUB, or use Open ReDUB Launcher.cmd,\nto return to this launcher. Missing game files always show setup.",10,false);hint.ForeColor=Muted;hint.SetBounds(24,y+70,Math.Max(400,card.Width-48),130);hint.Anchor=AnchorStyles.Top|AnchorStyles.Left|AnchorStyles.Right;card.Controls.Add(hint);
        }
        if(cat=="Advanced"){
            var warning=LabelAt("Rebuilding compiles the game port on this PC. It needs MSYS2, about 10 GB of free space and 20+ minutes. Normal setup never compiles anything.",10,false);warning.ForeColor=Color.FromArgb(245,180,120);warning.SetBounds(24,y,Math.Max(400,card.Width-48),72);warning.Anchor=AnchorStyles.Top|AnchorStyles.Left|AnchorStyles.Right;card.Controls.Add(warning);y+=80;
            ButtonAt(card,"Rebuild from source",24,y,250,()=>ConfirmRebuild(true),true);y+=54;
            ButtonAt(card,"Repair build",24,y,250,()=>ConfirmRebuild(false));y+=54;
            ButtonAt(card,"Check prerequisites",24,y,250,()=>Run(".portable/Build.ps1","-Mode","Doctor"));y+=62;
            var hint=LabelAt("Use Install for normal setup. Rebuild is for a source checkout, a damaged runtime, or renderer development. Game files and saves are kept.",10,false);hint.ForeColor=Muted;hint.SetBounds(24,y,Math.Max(400,card.Width-48),80);hint.Anchor=AnchorStyles.Top|AnchorStyles.Left|AnchorStyles.Right;card.Controls.Add(hint);
        }

    }
    void Choice(Control host,ref int y,string name,string key,string[] labels,string[] vals,string fallback){
        var title=LabelAt(name,10,false);title.SetBounds(24,y,220,38);host.Controls.Add(title);
        int x=Math.Max(245,host.Width-270);string value=ReadSetting(key,fallback);int ix=Array.IndexOf(vals,value);
        if(ix<0){double target;if(double.TryParse(value,System.Globalization.NumberStyles.Float,System.Globalization.CultureInfo.InvariantCulture,out target))for(int i=0;i<vals.Length;i++){double v;if(double.TryParse(vals[i],System.Globalization.NumberStyles.Float,System.Globalization.CultureInfo.InvariantCulture,out v)&&Math.Abs(v-target)<.001)ix=i;}}if(ix<0)ix=0;
        var label=LabelAt(labels[ix],10,true);label.SetBounds(x+42,y-4,160,40);label.TextAlign=ContentAlignment.MiddleCenter;label.Anchor=AnchorStyles.Right|AnchorStyles.Top;host.Controls.Add(label);
        Action<int> change=delta=>{try{if(GameRunning()){MessageBox.Show(this,"Save and close the game first. In-game Port Options can apply live changes.","Game is running");return;}int next=(ix+delta+vals.Length)%vals.Length;SavePreference(key,vals[next]);ix=next;label.Text=labels[ix];if(key=="MC3_GRAPHICS_PRESET")ShowSettings(category);else if(key=="MC3_WHEEL")ShowSettings("Controls");}catch(Exception ex){MessageBox.Show(this,ex.Message,"Could not save settings",MessageBoxButtons.OK,MessageBoxIcon.Error);}};
        var left=ButtonAt(host,"‹",x,y-4,38,()=>change(-1));left.Anchor=AnchorStyles.Right|AnchorStyles.Top;left.AccessibleName="Decrease "+name;
        var right=ButtonAt(host,"›",x+206,y-4,38,()=>change(1));right.Anchor=AnchorStyles.Right|AnchorStyles.Top;right.AccessibleName="Increase "+name;
        y+=62;
    }
    string SettingsPath(){string dir=RendererDist(root),selected=Path.Combine(dir,"mcdub3remix_env.txt");if(!File.Exists(selected)){string example=Path.Combine(dir,"mcdub3remix_env.example.txt");if(File.Exists(example)){try{File.Copy(example,selected);}catch{}}}if(File.Exists(selected))return selected;return PathAt("work-remix/build-windows-x86_64/dist/mcdub3remix_env.txt");}
    public string ReadSetting(string key,string fallback){if(!File.Exists(SettingsPath()))return fallback;foreach(var raw in File.ReadAllLines(SettingsPath())){string line=raw.Trim();if(line.StartsWith(key+"=",StringComparison.Ordinal))return line.Substring(key.Length+1).Trim();}return fallback;}
    public void SavePreference(string key,string value){string path=SettingsPath();if(!File.Exists(path))throw new IOException("Install the game before saving settings.");var edits=new Dictionary<string,string>();edits[key]=value;if(key=="MC3_GRAPHICS_PRESET"&&value!="0"){int n=int.Parse(value);edits["RECOMP_GL_SCALE"]=n==1?"1.5":n==3?"3":"2";edits["RECOMP_GL_FXAA"]="1";edits["RECOMP_GL_ANISO"]=n==1?"4":"16";edits["RECOMP_GL_SHARPEN"]=n==1?"0.25":"0.5";edits["RECOMP_GL_VSYNC"]=n==3?"1":"0";}else if(new[]{"RECOMP_GL_SCALE","RECOMP_GL_FXAA","RECOMP_GL_ANISO","RECOMP_GL_SHARPEN","RECOMP_GL_VSYNC"}.Contains(key))edits["MC3_GRAPHICS_PRESET"]="0";var lines=File.ReadAllLines(path).Where(line=>!edits.Keys.Any(k=>line.TrimStart().StartsWith(k+"=",StringComparison.Ordinal))).ToList();foreach(var e in edits)lines.Add(e.Key+"="+e.Value);string backup=path+".launcher-backup";if(!File.Exists(backup))File.Copy(path,backup);string tmp=path+".launcher-tmp";File.WriteAllLines(tmp,lines,new UTF8Encoding(false));File.Replace(tmp,path,null);Append("Saved "+key+". Applies next launch.");}
    void ShowSupport(){var p=Standard("Support","Support & activity","Local setup output. Your game and saves are never uploaded.");ButtonAt(p,"Open log folder",36,140,190,()=>Open("work-remix/logs"));ButtonAt(p,"Check setup",240,140,190,()=>VerifySetup());ButtonAt(p,"Read guide",444,140,190,()=>Open("README.md"));log.SetBounds(36,205,Math.Max(600,content.Width-72),Math.Max(280,content.Height-245));log.Anchor=AnchorStyles.Top|AnchorStyles.Left|AnchorStyles.Right|AnchorStyles.Bottom;p.Controls.Add(log);}
    void ShowAbout(){var p=Standard("About","Midnight Club 3: ReDUB","A community edition of the original Xbox DUB Edition Remix.");var copy=LabelAt("Built on mcdub3-nx and xboxrecomp.\n\nNative Port Options, independent HUD quality, PC Music integration,\nand targeted controller, menu and GPU stability fixes.\n\nOriginal 30 FPS simulation\n\nNo game data is included. Use your own supported Xbox Remix disc.\nNot affiliated with Rockstar Games, Take-Two or Microsoft.\n\nLauncher design reference: SafeHouse by FridiNaTor1 and contributors.\nReDUB uses its own implementation and branding.",11,false);copy.SetBounds(38,150,790,330);p.Controls.Add(copy);ButtonAt(p,"Features & credits",36,510,230,()=>Open("README.md"));}
    string PathAt(string p){return Path.Combine(root,p.Replace('/',Path.DirectorySeparatorChar));}
    void UpdateStatus(){bool running=GameRunning(),hasDisc=File.Exists(PathAt("game-remix/default.xbe")),runtime=RuntimePresent(root);play.Enabled=job==null&&!working&&!running;play.Text=running?"Game is running":Ready()?"▶   Play":"Install";string active="OpenGL • Native";if(working){status.Text=importStatus.Length>0?importStatus:"Installing game files...";return;}status.Text=job!=null?"Working — setup progress is in Support.":running?"Game is running • Save and close before changing launcher settings or installing builds.":Ready()?"Ready to play • "+active+" • shared Career saves":!runtime?"Game runtime missing • use the full ReDUB Windows zip, or Settings → Advanced → Rebuild from source.":hasDisc?"Game files found • choose Play to finish setup":"Not installed • choose Install to copy your own Xbox Remix disc.";}
    void Append(string text){if(IsDisposed)return;if(InvokeRequired){BeginInvoke(new Action<string>(Append),text);return;}log.AppendText(text+Environment.NewLine);log.SelectionStart=log.TextLength;log.ScrollToCaret();}
    void PlayGame(){try{StartDirect(root);UpdateStatus();}catch(Exception x){MessageBox.Show(this,x.Message,"ReDUB");}}
    void VerifySetup(){
        string[] problems=InstallationProblems(root);
        if(problems.Length==0){Append("Setup verified.");MessageBox.Show(this,"Game files, game executable and controller runtime are present.","Setup verified",MessageBoxButtons.OK,MessageBoxIcon.Information);}
        else{foreach(string problem in problems)Append("Setup: "+problem);MessageBox.Show(this,string.Join("\n\n",problems),"Setup needs attention",MessageBoxButtons.OK,MessageBoxIcon.Warning);}
        UpdateStatus();
    }
    void ConfirmRebuild(bool rebuild){
        if(working||job!=null){MessageBox.Show(this,"Wait for the current operation to finish.","ReDUB");return;}
        if(GameRunning()){MessageBox.Show(this,"Save and close the game before rebuilding.","Game is running");return;}
        if(!File.Exists(PathAt("game-remix/default.xbe"))){MessageBox.Show(this,"Add your game files first. Rebuild only compiles the game port.","Game files are missing");ShowInstall();return;}
        string text=rebuild?"Rebuild the game from source?\n\nThis needs MSYS2, about 10 GB of free space and 20+ minutes. It does not change your game files or saves.":"Repair the installed build from source?\n\nThis needs MSYS2, about 10 GB of free space and 20+ minutes. It does not change your game files or saves.";
        if(MessageBox.Show(this,text,rebuild?"Rebuild from source":"Repair build",MessageBoxButtons.YesNo,MessageBoxIcon.Warning)!=DialogResult.Yes)return;
        Run(".portable/Build.ps1","-Mode",rebuild?"Rebuild":"Build");
    }
    void StartImport(string selected){
        if(working)return;
        if(job!=null){MessageBox.Show(this,"Wait for the current operation to finish.","ReDUB");return;}
        if(GameRunning()){MessageBox.Show(this,"Save and close the game before installing game files.","Game is running");return;}
        selected=(selected??"").Trim();
        if(selected.Length==0){MessageBox.Show(this,"Choose your own Xbox Remix ISO or extracted folder first.","Choose game files");return;}
        if(!File.Exists(selected)&&!Directory.Exists(selected)){MessageBox.Show(this,"The selected disc or folder does not exist.","Choose game files");return;}
        bool replace=File.Exists(PathAt("game-remix/default.xbe"));
        if(replace&&MessageBox.Show(this,"Replace the game files in game-remix?\n\nYour saves and settings are kept. The new files are checked before the old copy is removed.","Replace game files",MessageBoxButtons.YesNo,MessageBoxIcon.Warning)!=DialogResult.Yes)return;
        closeAfterImport=false;
        if(importCancellation!=null){try{importCancellation.Dispose();}catch{}}
        importCancellation=new CancellationTokenSource();
        working=true;importStatus="Installing game files... Press Esc to cancel.";if(importAction!=null&&!importAction.IsDisposed)importAction.Enabled=false;if(cancelImport!=null&&!cancelImport.IsDisposed){cancelImport.Visible=true;cancelImport.Enabled=true;}progress.Style=ProgressBarStyle.Marquee;progress.Visible=true;UpdateStatus();
        string sourcePath=selected;var ui=this;var cancellation=importCancellation;
        var thread=new Thread(delegate(){
            string edition=null,error=null;bool cancelled=false;
            try{edition=ImportGameFiles(root,sourcePath,replace,delegate(long done,long total,string name){if(ui.IsDisposed||!ui.IsHandleCreated)return;try{ui.BeginInvoke(new Action<long,long,string>(ui.UpdateImportProgress),done,total,name);}catch(InvalidOperationException){}},cancellation.Token);}
            catch(OperationCanceledException){cancelled=true;}
            catch(Exception ex){error=ex.Message;}
            if(!ui.IsDisposed&&ui.IsHandleCreated)ui.BeginInvoke(new Action<string,string,bool>(ui.FinishImport),edition,error,cancelled);
        });thread.IsBackground=true;
        try{thread.Start();}catch(Exception ex){working=false;try{cancellation.Dispose();}catch{} importCancellation=null;progress.Visible=false;if(importAction!=null&&!importAction.IsDisposed)importAction.Enabled=true;if(cancelImport!=null&&!cancelImport.IsDisposed){cancelImport.Visible=false;cancelImport.Enabled=false;}MessageBox.Show(this,ex.Message,"Install",MessageBoxButtons.OK,MessageBoxIcon.Error);UpdateStatus();}
    }
    void CancelImport(){
        if(!working||importCancellation==null)return;
        if(cancelImport!=null&&!cancelImport.IsDisposed)cancelImport.Enabled=false;
        importStatus="Cancelling game file import...";UpdateStatus();
        try{importCancellation.Cancel();}catch(ObjectDisposedException){}
    }
    void UpdateImportProgress(long done,long total,string name){
        if(total>0){progress.Style=ProgressBarStyle.Continuous;progress.Minimum=0;progress.Maximum=1000;int value=(int)Math.Min(1000.0,done*1000.0/total);progress.Value=Math.Max(0,value);importStatus="Installing game files - "+(int)(done*100.0/total)+"% — "+Path.GetFileName(name);}
        else importStatus="Installing game files...";
        status.Text=importStatus;
    }
    void FinishImport(string edition,string error,bool cancelled){
        working=false;progress.Visible=false;progress.Style=ProgressBarStyle.Marquee;progress.Value=0;importStatus="";cancelImport=null;
        if(importCancellation!=null){try{importCancellation.Dispose();}catch{} importCancellation=null;}
        if(cancelled){
            Append("Install cancelled. The previous game files were left untouched.");
            if(closeAfterImport){closeAfterImport=false;Close();return;}
            MessageBox.Show(this,"Installing game files was cancelled. The previous game files were left untouched.","Install",MessageBoxButtons.OK,MessageBoxIcon.Information);
            UpdateStatus();if(page=="Install")ShowInstall();return;
        }
        if(error!=null){Append("Install failed: "+error);if(closeAfterImport){closeAfterImport=false;Close();return;}MessageBox.Show(this,error,"Install",MessageBoxButtons.OK,MessageBoxIcon.Error);UpdateStatus();if(page=="Install")ShowInstall();return;}
        Append("Game files installed"+(edition!=null?" ("+edition+" edition)":"")+".");
        if(!RuntimePresent(root))Append("The game executable is missing from this release folder. Use the full ReDUB Windows zip, or rebuild from source in Settings → Advanced.");
        if(closeAfterImport){closeAfterImport=false;Close();return;}
        UpdateStatus();ShowHome();
    }
    public static string Quote(string value){var b=new StringBuilder("\"");int slashes=0;foreach(char c in value){if(c=='\\'){slashes++;continue;}if(c=='\"'){b.Append('\\',slashes*2+1);b.Append(c);}else{b.Append('\\',slashes);b.Append(c);}slashes=0;}b.Append('\\',slashes*2);return b.Append('"').ToString();}
    void Run(string script,params string[] args){
        if(job!=null||working){MessageBox.Show(this,"Wait for the current operation to finish.","ReDUB");return;}
        var ps=new ProcessStartInfo {FileName=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System),@"WindowsPowerShell\v1.0\powershell.exe"),WorkingDirectory=root,UseShellExecute=false,CreateNoWindow=true,RedirectStandardOutput=true,RedirectStandardError=true};
        ps.Arguments="-NoLogo -NoProfile -ExecutionPolicy Bypass -File "+Quote(PathAt(script));foreach(string a in args)ps.Arguments+=" "+Quote(a);
        if(script!=".portable/Start.ps1")ShowSupport();var p=new Process {StartInfo=ps};job=p;progress.Visible=true;Append("Starting "+Path.GetFileName(script)+"...");UpdateStatus();
        p.OutputDataReceived+=(s,e)=>{if(e.Data!=null)Append(e.Data);};p.ErrorDataReceived+=(s,e)=>{if(e.Data!=null)Append(e.Data);};
        try{p.Start();p.BeginOutputReadLine();p.BeginErrorReadLine();}catch{job=null;progress.Visible=false;p.Dispose();throw;}
        System.Threading.ThreadPool.QueueUserWorkItem(_=>{p.WaitForExit();int code=p.ExitCode;if(!IsDisposed)BeginInvoke(new Action(()=>{Append(code==0?"Operation complete.":"Operation failed. Review the messages above and the Logs folder.");job=null;progress.Visible=false;p.Dispose();UpdateStatus();}));});
    }
    void Open(string path){string full=PathAt(path);if(!File.Exists(full)&&!Directory.Exists(full))throw new IOException("This item is not present yet: "+path);if(File.Exists(full))Process.Start("notepad.exe",Quote(full));else Process.Start(new ProcessStartInfo(full){UseShellExecute=true});}
    static string PreferencesPath(string root){return Path.Combine(root,"launcher","preferences.ini");}
    static string LauncherValue(string root,string key,string fallback){try{string path=PreferencesPath(root);if(File.Exists(path))foreach(string raw in File.ReadAllLines(path)){string line=raw.Trim();if(line.StartsWith(key+"=",StringComparison.OrdinalIgnoreCase))return line.Substring(key.Length+1).Trim();}}catch{}return fallback;}
    static void SaveLauncherValue(string root,string key,string value){string path=PreferencesPath(root);Directory.CreateDirectory(Path.GetDirectoryName(path));var lines=File.Exists(path)?File.ReadAllLines(path).Where(line=>!line.TrimStart().StartsWith(key+"=",StringComparison.OrdinalIgnoreCase)).ToList():new List<string>();lines.Add(key+"="+value);string tmp=path+".tmp";File.WriteAllLines(tmp,lines,new UTF8Encoding(false));if(File.Exists(path))File.Replace(tmp,path,null);else File.Move(tmp,path);}
    public static bool BypassEnabled(string root){return LauncherValue(root,"BypassLauncher","0")=="1";}
    public static void SetBypass(string root,bool enabled){SaveLauncherValue(root,"BypassLauncher",enabled?"1":"0");}
    public static string RendererDist(string root){return Path.Combine(root,@"work-remix\build-windows-x86_64\dist");}
    public static bool ShouldBypass(bool enabled,string[] args,bool shiftHeld,bool ready){return ready&&!shiftHeld&&!args.Contains("--launcher",StringComparer.OrdinalIgnoreCase)&&(enabled||args.Contains("--play",StringComparer.OrdinalIgnoreCase));}
    public static bool InstallationReady(string root){string dist=RendererDist(root);return File.Exists(Path.Combine(dist,"mcdub3remix.exe"))&&File.Exists(Path.Combine(dist,"SDL2.dll"))&&File.Exists(Path.Combine(root,@"game-remix\default.xbe"));}
    public static bool RuntimePresent(string root){string dist=Path.Combine(root,@"work-remix\build-windows-x86_64\dist");return File.Exists(Path.Combine(dist,"mcdub3remix.exe"))&&File.Exists(Path.Combine(dist,"SDL2.dll"));}
    public static string[] InstallationProblems(string root){
        var problems=new List<string>();
        string game=Path.Combine(root,@"game-remix"),xbe=Path.Combine(game,"default.xbe");
        if(!File.Exists(xbe))problems.Add("Game files are missing: "+xbe+". Choose Install.");
        else{string edition,error;if(!Xdvdfs.TryVerify(game,out edition,out error))problems.Add(error);}
        string dist=RendererDist(root),exe=Path.Combine(dist,"mcdub3remix.exe"),sdl=Path.Combine(dist,"SDL2.dll");
        if(!File.Exists(exe))problems.Add("Game executable is missing: "+exe+". Use the full ReDUB Windows zip, or Settings → Advanced → Rebuild from source.");
        else if(!File.Exists(sdl))problems.Add("Controller runtime is missing: "+sdl+". Use the full ReDUB Windows zip, or Settings → Advanced → Rebuild from source.");
        return problems.ToArray();
    }
    /* Import is copy-only: an extracted folder is copied and an ISO is read by
     * the in-house XDVDFS reader. Both land in a staging folder, the XBE is
     * verified, and only then is the old game-remix replaced. */
    public static string ImportGameFiles(string root,string source,bool replaceExisting,Action<long,long,string> progress,CancellationToken cancellationToken){
        string fullSource=Path.GetFullPath(source),game=Path.Combine(root,"game-remix"),staging=Path.Combine(root,"game-remix.importing");
        string sourcePrefix=fullSource.TrimEnd(Path.DirectorySeparatorChar,Path.AltDirectorySeparatorChar)+Path.DirectorySeparatorChar;
        string rootPrefix=Path.GetFullPath(root).TrimEnd(Path.DirectorySeparatorChar,Path.AltDirectorySeparatorChar)+Path.DirectorySeparatorChar;
        string gamePrefix=game.TrimEnd(Path.DirectorySeparatorChar,Path.AltDirectorySeparatorChar)+Path.DirectorySeparatorChar;
        if(File.Exists(game))throw new IOException("game-remix exists as a file. Remove it or move it away first.");
        if(!replaceExisting&&File.Exists(Path.Combine(game,"default.xbe")))throw new IOException("Game files are already installed.");
        if(rootPrefix.StartsWith(sourcePrefix,StringComparison.OrdinalIgnoreCase))throw new IOException("Choose the disc folder, not a folder that contains ReDUB.");
        if(sourcePrefix.StartsWith(gamePrefix,StringComparison.OrdinalIgnoreCase))throw new IOException("Choose the disc folder, not the installed game-remix folder.");
        TryDelete(staging);
        try{
            if(Directory.Exists(fullSource)){
                if(!File.Exists(Path.Combine(fullSource,"default.xbe")))throw new IOException("The extracted folder must contain default.xbe.");
                CopyFolder(fullSource,staging,progress,cancellationToken);
            }else if(File.Exists(fullSource)){
                string extension=Path.GetExtension(fullSource);if(!string.Equals(extension,".iso",StringComparison.OrdinalIgnoreCase)&&!string.Equals(extension,".xiso",StringComparison.OrdinalIgnoreCase))throw new IOException("Choose an Xbox disc image (.iso or .xiso) or an extracted folder containing default.xbe.");
                Xdvdfs.Extract(fullSource,staging,progress,cancellationToken);
            }else throw new IOException("The selected disc or folder does not exist.");
            cancellationToken.ThrowIfCancellationRequested();
            string edition=Xdvdfs.Verify(staging);
            cancellationToken.ThrowIfCancellationRequested();
            string old=null;
            if(Directory.Exists(game)){old=game+".old-"+DateTime.Now.ToString("yyyyMMdd-HHmmss-fff");Directory.Move(game,old);}
            try{Directory.Move(staging,game);}
            catch{if(old!=null){try{Directory.Move(old,game);}catch{}}throw;}
            if(old!=null)try{Directory.Delete(old,true);}catch{}
            return edition;
        }catch{TryDelete(staging);throw;}
    }
    static void CopyFolder(string source,string destination,Action<long,long,string> progress,CancellationToken cancellationToken){
        string sourceRoot=source.TrimEnd(Path.DirectorySeparatorChar,Path.AltDirectorySeparatorChar);
        string[] files=Directory.GetFiles(sourceRoot,"*",SearchOption.AllDirectories);
        long total=0,done=0;foreach(string file in files)total+=new FileInfo(file).Length;
        Directory.CreateDirectory(destination);
        foreach(string directory in Directory.GetDirectories(sourceRoot,"*",SearchOption.AllDirectories))Directory.CreateDirectory(Path.Combine(destination,directory.Substring(sourceRoot.Length).TrimStart(Path.DirectorySeparatorChar,Path.AltDirectorySeparatorChar)));
        byte[] buffer=new byte[1024*1024];
        foreach(string file in files){
            cancellationToken.ThrowIfCancellationRequested();
            string relative=file.Substring(sourceRoot.Length).TrimStart(Path.DirectorySeparatorChar,Path.AltDirectorySeparatorChar),target=Path.Combine(destination,relative);
            using(var input=new FileStream(file,FileMode.Open,FileAccess.Read,FileShare.Read,65536,FileOptions.SequentialScan))
            using(var output=new FileStream(target,FileMode.CreateNew,FileAccess.Write,FileShare.None,65536,FileOptions.SequentialScan)){
                int got;while((got=input.Read(buffer,0,buffer.Length))>0){cancellationToken.ThrowIfCancellationRequested();output.Write(buffer,0,got);done+=got;if(progress!=null)progress(done,total,relative);}
            }
            try{File.SetLastWriteTimeUtc(target,File.GetLastWriteTimeUtc(file));}catch{}
        }
    }
    static void TryDelete(string path){try{if(Directory.Exists(path))Directory.Delete(path,true);}catch{}}
    public static ProcessStartInfo LaunchInfo(string root){
        
        string dist=RendererDist(root);
        return new ProcessStartInfo(Path.Combine(dist,"mcdub3remix.exe")){WorkingDirectory=dist,UseShellExecute=true};
    }
    static void StartDirect(string root){
        var running=Process.GetProcessesByName("mcdub3remix");bool active=running.Length>0;foreach(var p in running)p.Dispose();if(active)return;
        if(!InstallationReady(root)){string[] problems=InstallationProblems(root);throw new IOException(problems.Length>0?problems[0]:"The game is not ready. Choose Install.");}
        /* The game is a window program: started on its own its output goes
         * nowhere, so a crash report sent from RecompLauncher arrives with no
         * log. cmd holds the redirect to work-remix/logs/latest.log, and stays
         * alive after this launcher exits, so the file outlives us. */
        string dist=RendererDist(root),log=RunLogPath(root);
        try{if(File.Exists(log))File.Copy(log,log+".previous",true);}catch{}
        var psi=new ProcessStartInfo(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System),"cmd.exe")){
            Arguments="/s /c \"\""+Path.Combine(dist,"mcdub3remix.exe")+"\" > \""+log+"\" 2>&1\"",
            WorkingDirectory=dist,UseShellExecute=false,CreateNoWindow=true};
        Process.Start(psi);
    }
    static string RunLogPath(string root){string dir=Path.Combine(root,@"work-remix\logs");Directory.CreateDirectory(dir);return Path.Combine(dir,"latest.log");}
    public static bool IsSourceRoot(string path){return Directory.Exists(path)&&File.Exists(Path.Combine(path,@".portable\Build.ps1"))&&File.Exists(Path.Combine(path,"CMakeLists.txt"));}
    static void CliLog(string root,string message){try{string dir=Path.Combine(root,@"work-remix\logs");Directory.CreateDirectory(dir);File.AppendAllText(Path.Combine(dir,"launcher-cli.log"),DateTime.Now.ToString("s")+" "+message+Environment.NewLine,new UTF8Encoding(false));}catch{}}
    static int RunCommand(string root,string command,string[] args){
        try{
            if(command=="--import"){
                if(args.Length<2){CliLog(root,"--import needs a folder or .iso path");return 2;}
                /* Launchers run this without a window and read progress lines from
                 * stdout: "progress 0.437 Copying name". Percent steps keep the
                 * output small on a multi-gigabyte copy. */
                long last=-1;
                Action<long,long,string> report=(done,total,name)=>{
                    if(total<=0)return;
                    long step=done*1000/total;
                    if(step==last)return;
                    last=step;
                    string fraction=((double)step/1000.0).ToString("0.000",System.Globalization.CultureInfo.InvariantCulture);
                    try{Console.Out.WriteLine("progress "+fraction+" "+Path.GetFileName(name));Console.Out.Flush();}catch{}
                };
                string edition=ImportGameFiles(root,args[1],true,report,CancellationToken.None);
                CliLog(root,"Imported game files ("+edition+").");return 0;
            }
            if(command=="--play"){StartDirect(root);CliLog(root,"Started the game.");return 0;}
            if(command=="--check"){
                string[] problems=InstallationProblems(root);
                if(problems.Length==0){CliLog(root,"Setup OK.");return 0;}
                foreach(string problem in problems)CliLog(root,"FAIL: "+problem);return 1;
            }
            if(command=="--build")return RunBuild(root);
        }catch(Exception ex){CliLog(root,command+" failed: "+ex.Message);return 1;}
        CliLog(root,"Unknown command: "+command);return 2;
    }
    static int RunBuild(string root){
        string script=Path.Combine(root,@".portable\Build.ps1");
        if(!File.Exists(script)){CliLog(root,"Build.ps1 is missing.");return 1;}
        var ps=new ProcessStartInfo {FileName=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System),@"WindowsPowerShell\v1.0\powershell.exe"),WorkingDirectory=root,UseShellExecute=false,CreateNoWindow=true,RedirectStandardOutput=true,RedirectStandardError=true};
        ps.Arguments="-NoLogo -NoProfile -ExecutionPolicy Bypass -File "+Quote(script)+" -Mode Rebuild";
        var p=new Process {StartInfo=ps};p.OutputDataReceived+=(s,e)=>{if(e.Data!=null)CliLog(root,e.Data);};p.ErrorDataReceived+=(s,e)=>{if(e.Data!=null)CliLog(root,e.Data);};
        p.Start();p.BeginOutputReadLine();p.BeginErrorReadLine();p.WaitForExit();int code=p.ExitCode;p.Dispose();CliLog(root,"Build exit code "+code+".");return code;
    }
    static string Sha256(string path){using(var sha=System.Security.Cryptography.SHA256.Create())using(var stream=File.OpenRead(path))return BitConverter.ToString(sha.ComputeHash(stream)).Replace("-","").ToLowerInvariant();}
    /* The setup source travels as launcher-source.zip beside the launcher, not
     * inside the executable. A large embedded archive made every security
     * scanner treat the launcher as a packed dropper; an embedded copy is only
     * a fallback for builds that still carry one. */
    static Stream OpenSourceBundle(){
        string[] candidates={Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"launcher-source.zip"),Path.Combine(AppDomain.CurrentDomain.BaseDirectory,@"work-remix\launcher-source.zip")};
        foreach(string path in candidates)if(File.Exists(path))return File.OpenRead(path);
        return null;
    }
    public static void ExtractSource(string destination){
        string prefix=Path.GetFullPath(destination).TrimEnd(Path.DirectorySeparatorChar)+Path.DirectorySeparatorChar;
        string managed=Path.Combine(destination,".redub-source-manifest.txt");var previous=new Dictionary<string,string>(StringComparer.OrdinalIgnoreCase);
        if(File.Exists(managed))foreach(string line in File.ReadAllLines(managed)){int tab=line.IndexOf('\t');if(tab>0)previous[line.Substring(tab+1).Replace('/',Path.DirectorySeparatorChar)]=line.Substring(0,tab);}
        using(var stream=OpenSourceBundle()){
            if(stream==null)throw new IOException("This folder has no setup source. Put launcher-source.zip next to ReDUB Launcher.exe and start it again.");
            using(var archive=new ZipArchive(stream,ZipArchiveMode.Read)){
                var current=new List<string>();string manifestText=null;var manifestEntry=archive.GetEntry("launcher-source-manifest.json");
                if(manifestEntry!=null)using(var reader=new StreamReader(manifestEntry.Open(),Encoding.UTF8))manifestText=reader.ReadToEnd();
                if(manifestText!=null){foreach(System.Text.RegularExpressions.Match match in System.Text.RegularExpressions.Regex.Matches(manifestText,@"""path""\s*:\s*""([^""]+)""[\s\S]*?""sha256""\s*:\s*""([a-f0-9]{64})""",System.Text.RegularExpressions.RegexOptions.IgnoreCase))current.Add(match.Groups[2].Value.ToLowerInvariant()+"\t"+match.Groups[1].Value.Replace('/',Path.DirectorySeparatorChar));}
                foreach(var entry in archive.Entries){
                if(entry.FullName=="launcher-source-manifest.json")continue;
                string target=Path.GetFullPath(Path.Combine(prefix,entry.FullName.Replace('/',Path.DirectorySeparatorChar)));
                if(!target.StartsWith(prefix,StringComparison.OrdinalIgnoreCase))throw new IOException("Invalid embedded source path.");
                if(entry.FullName.EndsWith("/")){Directory.CreateDirectory(target);continue;}
                Directory.CreateDirectory(Path.GetDirectoryName(target));
                string oldHash;bool replace=File.Exists(target)&&previous.TryGetValue(entry.FullName.Replace('/',Path.DirectorySeparatorChar),out oldHash)&&String.Equals(Sha256(target),oldHash,StringComparison.OrdinalIgnoreCase);
                if(File.Exists(target)&&!replace)continue;
                string temp=target+".redub-new";try{using(var input=entry.Open())using(var output=File.Create(temp))input.CopyTo(output);if(File.Exists(target))File.Replace(temp,target,null);else File.Move(temp,target);}finally{if(File.Exists(temp))File.Delete(temp);}
                }
                if(current.Count>0)File.WriteAllLines(managed,current,new UTF8Encoding(false));
            }
        }
        if(!IsSourceRoot(destination))throw new IOException("The setup source is incomplete.");
    }
    public static string ResolveRoot(){
        string here=AppDomain.CurrentDomain.BaseDirectory;
        // A standalone launcher belongs to the folder the user put it in.
        // Never silently redirect builds or settings to a previous installation.
        // Always update managed files when the embedded bundle changes; keep user edits.
        ExtractSource(here);
        return here;
    }
    [STAThread] public static void Main(string[] args){string root;
        try{root=ResolveRoot();}catch(Exception ex){MessageBox.Show(ex.Message,"ReDUB setup");return;}
        if(args.Length>0&&args[0]=="--self-test"){if(Quote(@"D:\folder with spaces\")!="\"D:\\folder with spaces\\\\\"")Environment.Exit(2);if(!File.Exists(Path.Combine(root,@".portable\Build.ps1")))Environment.Exit(3);Directory.CreateDirectory(Path.Combine(root,@"work-remix\logs"));File.WriteAllText(Path.Combine(root,@"work-remix\logs\launcher-self-test.log"),"PASS: relative project discovery and Windows argument quoting");return;}
        if(args.Length>0){string command=args[0].ToLowerInvariant();if(command=="--import"||command=="--play"||command=="--check"||command=="--build")Environment.Exit(RunCommand(root,command,args));}
        string startupError=null;
        if(ShouldBypass(BypassEnabled(root),args,(Control.ModifierKeys&Keys.Shift)!=0,InstallationReady(root))){try{StartDirect(root);return;}catch(Exception ex){startupError="Direct launch failed: "+ex.Message;}}
        Application.EnableVisualStyles();Application.SetCompatibleTextRenderingDefault(false);var app=new Launcher(root);if(startupError!=null)app.Append(startupError);Application.Run(app);
    }
}
}


