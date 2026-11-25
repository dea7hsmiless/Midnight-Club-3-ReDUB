using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.Sockets;
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
[assembly: AssemblyVersion("0.2.5.0")]
[assembly: AssemblyFileVersion("0.2.5.0")]
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
    public Launcher(string directory){root=directory;try{var iconPath=Path.Combine(root,"launcher","art","redub.ico");this.Icon=File.Exists(iconPath)?new Icon(iconPath):System.Drawing.Icon.ExtractAssociatedIcon(Application.ExecutablePath);}catch{}Text="Midnight Club 3: ReDUB";Size=new Size(1200,800);MinimumSize=new Size(1000,700);StartPosition=FormStartPosition.CenterScreen;BackColor=Bg;ForeColor=Color.FromArgb(235,239,247);Font=new Font("Segoe UI",10);AutoScaleMode=AutoScaleMode.Dpi;KeyPreview=true;KeyDown+=(s,e)=>{if(working&&e.KeyCode==Keys.Escape){CancelImport();e.Handled=true;}};
        var shell=new TableLayoutPanel {Dock=DockStyle.Fill,ColumnCount=2,RowCount=2,Margin=Padding.Empty,Padding=Padding.Empty};shell.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute,220));shell.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,100));shell.RowStyles.Add(new RowStyle(SizeType.Percent,100));shell.RowStyles.Add(new RowStyle(SizeType.Absolute,38));Controls.Add(shell);
        var rail=new Panel {Dock=DockStyle.Fill,BackColor=Rail,Margin=Padding.Empty,Padding=new Padding(16)};shell.Controls.Add(rail,0,0);var mark=BrandPanel.LoadArt("logo.png");
        if(mark!=null){var brand=new PictureBox {Image=mark,SizeMode=PictureBoxSizeMode.Zoom,BackColor=Rail,AccessibleName="Midnight Club 3 ReDUB logo",TabStop=false};brand.SetBounds(16,24,188,84);brand.Disposed+=(sender,e)=>mark.Dispose();rail.Controls.Add(brand);}
        else{var brand=LabelAt("ReDUB",25,true);brand.SetBounds(16,30,188,50);brand.ForeColor=Color.FromArgb(245,80,96);rail.Controls.Add(brand);}
        Nav(rail,"Game",16,118,()=>ShowHome());Nav(rail,"Online",16,180,()=>ShowOnline());Nav(rail,"Install",16,242,()=>ShowInstall());Nav(rail,"Settings",16,304,()=>ShowSettings(category));
        var bottom=new Panel {Dock=DockStyle.Bottom,Height=120};rail.Controls.Add(bottom);Nav(bottom,"Support",0,4,()=>ShowSupport());Nav(bottom,"About",0,62,()=>ShowAbout());
        content.Dock=DockStyle.Fill;content.Margin=Padding.Empty;shell.Controls.Add(content,1,0);
        var footer=new Panel {Dock=DockStyle.Fill,BackColor=Color.FromArgb(14,22,39),Margin=Padding.Empty};shell.Controls.Add(footer,0,1);shell.SetColumnSpan(footer,2);status.SetBounds(20,9,930,22);status.Anchor=AnchorStyles.Left|AnchorStyles.Top|AnchorStyles.Right;status.ForeColor=Muted;status.Font=new Font("Segoe UI",9);footer.Controls.Add(status);progress.SetBounds(980,12,165,14);progress.Anchor=AnchorStyles.Right|AnchorStyles.Top;progress.Style=ProgressBarStyle.Marquee;progress.Visible=false;footer.Controls.Add(progress);
        source.BackColor=Card;source.ForeColor=ForeColor;source.BorderStyle=BorderStyle.FixedSingle;source.Font=Font;log.ReadOnly=true;log.BackColor=Rail;log.ForeColor=ForeColor;log.BorderStyle=BorderStyle.None;log.Font=new Font("Consolas",9);
        play.Click+=(s,e)=>{if(Ready())PlayGame();else ShowInstall();};Append("ReDUB launcher ready. Setup, checks and build output appear here.");Append("Installation folder: "+root);ShowHome();refresh.Interval=1500;refresh.Tick+=(s,e)=>{UpdateStatus();RefreshOnline(false);};refresh.Start();UpdateStatus();if(ShowOnlinePage)ShowOnline();if(InviteOnStart)InviteFriends();if(JoinOnStart>=0)OnlineAction("join","{\"room\":"+JoinOnStart.ToString()+"}");if(InviteRoom>=0){try{SavePreference("MC3_ROAM_ROOM",InviteRoom.ToString());Append("Invite: room "+InviteRoom+" saved.");}catch{}ShowOnline();if(Ready())PlayGame();}FormClosing+=(s,e)=>{
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
        var right=new Panel {Dock=DockStyle.Fill,Margin=Padding.Empty,BackColor=Bg};bottom.Controls.Add(right,1,0);play.Text=ready?"▶   Play":"Install";play.SetBounds(0,22,288,58);right.Controls.Add(play);ButtonAt(right,"Verify setup",0,92,140,()=>VerifySetup());ButtonAt(right,"Repair",148,92,140,()=>ShowInstall());ButtonAt(right,"Game folder",0,146,140,()=>Open(RendererDist(root)));ButtonAt(right,"Add shortcut",148,146,140,()=>Run(".portable/Build-Launcher.ps1","-ShortcutsOnly"));string crash=LastCrash(root);if(crash!=null)ButtonAt(right,"Report the last crash",0,194,288,()=>ReportCrash(crash));UpdateStatus();}
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
    void ShowSettings(string cat){category=cat;var p=Standard("Settings","Settings",cat=="Startup"?"Startup options apply the next time you open ReDUB.":"Changes are saved for the next launch. Close the game before changing game settings here.");var categories=new[]{"Display","Graphics","Controls","Music","Mods","Advanced"};for(int i=0;i<categories.Length;i++){string c=categories[i];var b=ButtonAt(p,c,36,145+i*54,160,()=>ShowSettings(c));b.Selected=c==cat;}
        var card=new Panel {BackColor=Card,Location=new Point(220,145),Size=new Size(Math.Max(510,content.Width-256),Math.Max(420,content.Height-185)),Anchor=AnchorStyles.Top|AnchorStyles.Bottom|AnchorStyles.Left|AnchorStyles.Right,AutoScroll=true};p.Controls.Add(card);var heading=LabelAt(cat,17,true);heading.SetBounds(24,20,480,38);card.Controls.Add(heading);int y=84;
        if(cat=="Display"){Choice(card,ref y,"Window mode","MC3_FULLSCREEN",new[]{"Windowed","Borderless fullscreen","Exclusive fullscreen"},new[]{"0","1","2"},"0");Choice(card,ref y,"Resolution","MC3_RESOLUTION",new[]{"Automatic (desktop)","1280 × 720","1600 × 900","1920 × 1080","2560 × 1440","3840 × 2160"},new[]{"auto","1280x720","1600x900","1920x1080","2560x1440","3840x2160"},"auto");Choice(card,ref y,"Aspect ratio (restart)","RECOMP_GL_ASPECT",new[]{"Auto (16:9)","4:3","Stretch"},new[]{"auto","4:3","fill"},"auto");Choice(card,ref y,"VSync","RECOMP_GL_VSYNC",new[]{"Off","On","Adaptive"},new[]{"0","1","-1"},"0");Choice(card,ref y,"Gamma","MC3_GAMMA",new[]{"0.8","0.9","1.0","1.1","1.2","1.3","1.4"},new[]{"0.8","0.9","1","1.1","1.2","1.3","1.4"},"1");Choice(card,ref y,"Brightness","MC3_BRIGHTNESS",new[]{"80%","85%","90%","95%","100%","105%","110%","115%","120%"},new[]{"0.8","0.85","0.9","0.95","1","1.05","1.1","1.15","1.2"},"1");Choice(card,ref y,"Pause when the window loses focus","RECOMP_PAUSE_ON_FOCUS",new[]{"Off","On"},new[]{"0","1"},"0");Choice(card,ref y,"Loading screen minimum","RECOMP_LOADING_MIN_MS",new[]{"As the game decides","0.5 s","1 s","1.5 s","2 s","3 s"},new[]{"0","500","1000","1500","2000","3000"},"0");}
        
        if(cat=="Graphics"){Choice(card,ref y,"Preset","MC3_GRAPHICS_PRESET",new[]{"Custom","Performance","Balanced","Quality"},new[]{"0","1","2","3"},"0");Choice(card,ref y,"Internal resolution","RECOMP_GL_SCALE",new[]{"1× native (720 × 480)","1.5× native (1080 × 720)","2× native (1440 × 960)","3× native (2160 × 1440)","5× native (3600 × 2400)"},new[]{"1","1.5","2","3","5"},"2");Choice(card,ref y,"Frame generation","RECOMP_FG",new[]{"Off","Crossfade","Motion-compensated"},new[]{"0","1","2"},"0");Choice(card,ref y,"Frame rate","MC3_FRAME_RATE",new[]{"30 FPS (Xbox)","60 FPS","120 FPS (needs a 120 Hz display)"},new[]{"30","60","120"},"30");Choice(card,ref y,"HUD texture quality","RECOMP_GL_UI_SCALE",new[]{"1×","2×","3×","5×"},new[]{"1","2","3","5"},"2");Choice(card,ref y,"Anti-aliasing","RECOMP_GL_FXAA",new[]{"Off","FXAA Low","FXAA","FXAA High"},new[]{"0","2","1","3"},"1");Choice(card,ref y,"Texture filtering","RECOMP_GL_ANISO",new[]{"1×","4×","16×"},new[]{"1","4","16"},"16");Choice(card,ref y,"Sharpening","RECOMP_GL_SHARPEN",new[]{"Off","25%","50%","75%","100%"},new[]{"0","0.25","0.5","0.75","1"},"0.5");Choice(card,ref y,"Glow (restart)","RECOMP_GL_GLOW",new[]{"Off","25%","50%","75%","100%"},new[]{"0","0.25","0.5","0.75","1"},"0");Choice(card,ref y,"Vignette (restart)","RECOMP_GL_VIGNETTE",new[]{"Off","25%","50%","75%","100%"},new[]{"0","0.25","0.5","0.75","1"},"0");Choice(card,ref y,"Colour (restart)","RECOMP_GL_COLOUR",new[]{"Off","25%","50%","75%","100%"},new[]{"0","0.25","0.5","0.75","1"},"0");Choice(card,ref y,"Ambient occlusion (restart)","RECOMP_GL_AO",new[]{"Off","25%","50%","75%","100%"},new[]{"0","0.25","0.5","0.75","1"},"0");var scaleHint=LabelAt("Internal resolution multiplies the game's 720 × 480 surface. 3× is 2160 × 1440, 5× is 3600 × 2400 (4K-class detail).",9,false);scaleHint.ForeColor=Muted;scaleHint.SetBounds(24,y+8,Math.Max(400,card.Width-48),40);scaleHint.Anchor=AnchorStyles.Top|AnchorStyles.Left|AnchorStyles.Right;card.Controls.Add(scaleHint);y+=56;}
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
            ButtonAt(card,"Rebind keyboard…",24,y,240,()=>ShowKeyBindings());y+=58;
            var controlsHint=LabelAt("Xbox controllers use Windows gamepad support. Wheel compatibility depends on the model and its SDL input mapping. Rumble requires device support; full force-feedback steering is not included.",10,false);controlsHint.ForeColor=Muted;controlsHint.SetBounds(24,y+8,Math.Max(400,card.Width-48),64);controlsHint.Anchor=AnchorStyles.Top|AnchorStyles.Left|AnchorStyles.Right;card.Controls.Add(controlsHint);
        }
        if(cat=="Music"){Choice(card,ref y,"PC Music while driving","MC3_PC_MUSIC",new[]{"Off","On"},new[]{"0","1"},"0");Choice(card,ref y,"Song display","MC3_PC_MUSIC_HUD",new[]{"Overlay","Radio + fallback"},new[]{"0","1"},"1");Choice(card,ref y,"Player volume","MC3_PC_MUSIC_VOLUME",new[]{"Mute","10%","20%","30%","40%","50%","60%","70%","80%","90%","100%"},new[]{"0","10","20","30","40","50","60","70","80","90","100"},"100");var info=LabelAt("Uses built-in Windows media controls with supported players/services\nsuch as YouTube Music, Spotify and Pandora. No login is needed in ReDUB.\nMusic plays directly from your PC; ReDUB reads track info and controls\nsupported players. D-pad left/right requests track changes while driving.",10,false);info.ForeColor=Muted;info.SetBounds(24,y+10,Math.Max(400,card.Width-48),130);info.Anchor=AnchorStyles.Top|AnchorStyles.Left|AnchorStyles.Right;card.Controls.Add(info);}
        if(cat=="Mods"){
            var modsLabel=LabelAt("Unlock everything (restart)",10,false);modsLabel.SetBounds(24,y,300,38);card.Controls.Add(modsLabel);
            var modsToggle=ButtonAt(card,ReadSetting("MC3_UNLOCK_ALL","0")=="1"?"On":"Off",Math.Max(245,card.Width-270),y-4,244,()=>{});modsToggle.Anchor=AnchorStyles.Right|AnchorStyles.Top;
            modsToggle.Click+=(sender,e)=>{try{if(GameRunning()){MessageBox.Show(this,"Save and close the game first.","Game is running");return;}string next=ReadSetting("MC3_UNLOCK_ALL","0")=="1"?"0":"1";SavePreference("MC3_UNLOCK_ALL",next);modsToggle.Text=next=="1"?"On":"Off";}catch(Exception ex){MessageBox.Show(this,ex.Message,"Could not save settings",MessageBoxButtons.OK,MessageBoxIcon.Error);}};
            y+=62;
            var modsHint=LabelAt("Every car, part, rim, paint and driving ability is available, and every race and city is open. Your career save and money are untouched. Applies on the next launch; a car picks up an unlocked ability at its next build (a cruise load or a garage visit).",10,false);modsHint.ForeColor=Muted;modsHint.SetBounds(24,y,Math.Max(400,card.Width-48),110);modsHint.Anchor=AnchorStyles.Top|AnchorStyles.Left|AnchorStyles.Right;card.Controls.Add(modsHint);y+=118;
            var modsWhat=LabelAt("A mod changes what the game does: its rules, progression or content. Presentation, performance and startup options live under Settings. More mods will appear here, each off by default and restarting to apply.",10,false);modsWhat.ForeColor=Muted;modsWhat.SetBounds(24,y,Math.Max(400,card.Width-48),92);modsWhat.Anchor=AnchorStyles.Top|AnchorStyles.Left|AnchorStyles.Right;card.Controls.Add(modsWhat);
        }
        if(cat=="Advanced"){

            var diagHead=LabelAt("Diagnostics",14,true);diagHead.SetBounds(24,y,Math.Max(300,card.Width-48),30);card.Controls.Add(diagHead);y+=34;
            Choice(card,ref y,"FPS / frame time","MC3_FPS_OVERLAY",new[]{"Off","On"},new[]{"0","1"},"0");
            ButtonAt(card,"Open logs",24,y,150,()=>Open("work-remix/logs"));
            ButtonAt(card,"Check setup",184,y,170,()=>VerifySetup());
            y+=54;

            var startupHead=LabelAt("Startup",14,true);startupHead.SetBounds(24,y,Math.Max(300,card.Width-48),30);card.Controls.Add(startupHead);y+=34;
            Choice(card,ref y,"Skip intro movies (restart)","MC3_SKIP_MOVIES",new[]{"Off","On"},new[]{"0","1"},"0");
            var label=LabelAt("Skip launcher on startup",10,false);label.SetBounds(24,y,270,38);card.Controls.Add(label);
            var toggle=ButtonAt(card,BypassEnabled(root)?"On":"Off",Math.Max(245,card.Width-270),y-4,244,()=>{});toggle.Anchor=AnchorStyles.Right|AnchorStyles.Top;
            toggle.Click+=(sender,e)=>{try{SetBypass(root,!BypassEnabled(root));toggle.Text=BypassEnabled(root)?"On":"Off";}catch(Exception ex){MessageBox.Show(this,ex.Message,"Could not save launcher setting");}};
            y+=52;
            var startupHint=LabelAt("Cruise enters Arcade on its own; any input stops it. Skipping the intro presses through the boot movies. Hold Shift to return to this launcher.",9,false);startupHint.ForeColor=Muted;startupHint.SetBounds(24,y,Math.Max(400,card.Width-48),26);startupHint.Anchor=AnchorStyles.Top|AnchorStyles.Left|AnchorStyles.Right;card.Controls.Add(startupHint);y+=40;

            var rebuildHead=LabelAt("Repair and rebuild",14,true);rebuildHead.SetBounds(24,y,Math.Max(300,card.Width-48),30);card.Controls.Add(rebuildHead);y+=34;
            var warning=LabelAt("Rebuilding compiles the game port on this PC. It needs MSYS2, about 10 GB of free space and 20+ minutes.",9,false);warning.ForeColor=Color.FromArgb(245,180,120);warning.SetBounds(24,y,Math.Max(400,card.Width-48),44);warning.Anchor=AnchorStyles.Top|AnchorStyles.Left|AnchorStyles.Right;card.Controls.Add(warning);y+=50;
            ButtonAt(card,"Rebuild from source",24,y,170,()=>ConfirmRebuild(true),true);
            ButtonAt(card,"Repair build",204,y,150,()=>ConfirmRebuild(false));
            ButtonAt(card,"Check prerequisites",364,y,180,()=>Run(".portable/Build.ps1","-Mode","Doctor"));
            y+=50;
            var rebuildHint=LabelAt("Use Install for normal setup. Game files and saves are kept.",9,false);rebuildHint.ForeColor=Muted;rebuildHint.SetBounds(24,y,Math.Max(400,card.Width-48),26);rebuildHint.Anchor=AnchorStyles.Top|AnchorStyles.Left|AnchorStyles.Right;card.Controls.Add(rebuildHint);
        }
        card.AutoScrollMinSize=new Size(0,y+40);

    }
    /* ---- keyboard rebinding ---------------------------------------------
     * The game reads MC3_KEY_<ACTION> from the env file at start, as SDL
     * scancode names (sdl_keyboard.c). Capturing a key here writes that name;
     * a name the game cannot resolve keeps the default and says so in its log,
     * so a bad value here is reported rather than silently dead. */
    static readonly string[] KeyActions={"DPAD_UP","DPAD_DOWN","DPAD_LEFT","DPAD_RIGHT","START","BACK","LSTICK","RSTICK","A","B","X","Y","WHITE","BLACK","LT","RT","LSTICK_LEFT","LSTICK_RIGHT","LSTICK_UP","LSTICK_DOWN","RSTICK_LEFT","RSTICK_RIGHT","RSTICK_UP","RSTICK_DOWN"};
    static readonly string[] KeyDefaults={"I","K","J","L","Return","Backspace","Z","C","A","B","X","Y","Q","E","Down","Up","Left","Right","Up","Down","Keypad 4","Keypad 6","Keypad 8","Keypad 2"};
    static string SdlKeyName(Keys k){
        if(k>=Keys.A&&k<=Keys.Z)return ((char)('A'+(k-Keys.A))).ToString();
        if(k>=Keys.D0&&k<=Keys.D9)return ((char)('0'+(k-Keys.D0))).ToString();
        if(k>=Keys.NumPad0&&k<=Keys.NumPad9)return "Keypad "+(k-Keys.NumPad0);
        if(k>=Keys.F1&&k<=Keys.F12)return "F"+(((int)k-(int)Keys.F1)+1);
        switch(k){
            case Keys.Space:return "Space";case Keys.Enter:return "Return";case Keys.Tab:return "Tab";
            case Keys.Back:return "Backspace";case Keys.Escape:return "Escape";
            case Keys.Up:return "Up";case Keys.Down:return "Down";case Keys.Left:return "Left";case Keys.Right:return "Right";
            case Keys.LShiftKey:return "Left Shift";case Keys.RShiftKey:return "Right Shift";
            case Keys.LControlKey:return "Left Ctrl";case Keys.RControlKey:return "Right Ctrl";
            case Keys.LMenu:return "Left Alt";case Keys.RMenu:return "Right Alt";
            case Keys.OemMinus:return "-";case Keys.Oemplus:return "=";case Keys.Oemcomma:return ",";
            case Keys.OemPeriod:return ".";case Keys.OemQuestion:return "/";case Keys.OemSemicolon:return ";";
            case Keys.OemOpenBrackets:return "[";case Keys.OemCloseBrackets:return "]";case Keys.OemQuotes:return "'";
            case Keys.OemPipe:return "\\";case Keys.Oemtilde:return "`";
            case Keys.Insert:return "Insert";case Keys.Delete:return "Delete";case Keys.Home:return "Home";
            case Keys.End:return "End";case Keys.PageUp:return "PageUp";case Keys.PageDown:return "PageDown";
            case Keys.CapsLock:return "CapsLock";
        }
        return null;
    }
    void ShowKeyBindings(){
        if(GameRunning()){MessageBox.Show(this,"Save and close the game first: the layout is read when the game starts.","Game is running");return;}
        var f=new Form{Text="Keyboard (player 1)",ClientSize=new Size(520,660),StartPosition=FormStartPosition.CenterParent,FormBorderStyle=FormBorderStyle.FixedDialog,MaximizeBox=false,MinimizeBox=false,BackColor=Bg,ForeColor=Color.FromArgb(228,232,240)};
        var list=new Panel{Dock=DockStyle.Fill,AutoScroll=true,Padding=new Padding(16),BackColor=Bg};f.Controls.Add(list);
        var bar=new Panel{Dock=DockStyle.Bottom,Height=56,BackColor=Bg};f.Controls.Add(bar);
        var buttons=new Dictionary<string,RedubButton>();
        string capturing=null;
        Action redraw=()=>{
            int y=0;
            foreach(var action in KeyActions){
                string pretty=action.Replace("RSTICK","RIGHT STICK").Replace("LSTICK","LEFT STICK").Replace("DPAD","D-PAD").Replace('_',' ');
                var name=LabelAt(pretty,9,false);name.ForeColor=Color.FromArgb(228,232,240);name.SetBounds(0,y,200,36);list.Controls.Add(name);
                string current=ReadSetting("MC3_KEY_"+action,KeyDefaults[Array.IndexOf(KeyActions,action)]);
                if(current=="-1"||current=="")current="not set";
                RedubButton b;
                if(!buttons.TryGetValue(action,out b)){
                    b=ButtonAt(list,current,210,y,180,()=>{});
                    var captured=action;
                    b.Click+=(s,e)=>{capturing=captured;b.Text="Press a key…";};
                    buttons[action]=b;
                }else b.Text=current;
                y+=44;
            }
            list.AutoScrollMinSize=new Size(0,y+8);
        };
        redraw();
        f.KeyPreview=true;
        f.KeyDown+=(s,e)=>{
            if(capturing==null)return;
            e.Handled=true;e.SuppressKeyPress=true;
            if(e.KeyCode==Keys.Escape){capturing=null;redraw();return;}
            string n=SdlKeyName(e.KeyCode);
            if(n==null){MessageBox.Show(f,"That key cannot be sent to the game. Try a letter, a digit, an F-key, an arrow, or a modifier.","Keyboard");return;}
            try{SavePreference("MC3_KEY_"+capturing,n);capturing=null;redraw();}catch(Exception x){MessageBox.Show(f,x.Message,"Could not save",MessageBoxButtons.OK,MessageBoxIcon.Error);}
        };
        ButtonAt(bar,"Reset to defaults",16,8,200,()=>{try{for(int i=0;i<KeyActions.Length;i++)SavePreference("MC3_KEY_"+KeyActions[i],KeyDefaults[i]);redraw();Append("Keyboard layout reset to defaults.");}catch(Exception x){MessageBox.Show(f,x.Message,"Could not save",MessageBoxButtons.OK,MessageBoxIcon.Error);}});
        ButtonAt(bar,"Done",286,8,200,()=>f.Close());
        f.ShowDialog(this);
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
    public string ReadSetting(string key,string fallback){
        string value=ReadSettingFrom(SettingsPath(),key);
        if(value!=null)return value;
        value=ReadSettingFrom(Path.Combine(RendererDist(root),"mcdub3remix_defaults.txt"),key);
        if(value!=null)return value;
        return fallback;
    }
    /* One KEY = value per line, '#' comments, quotes around a value allowed,
     * and the last match wins -- the game reads its own settings the same way
     * (build defaults, then the env file), so the launcher shows what the game
     * will use rather than the caller's guess. */
    static string ReadSettingFrom(string path,string key){
        if(!File.Exists(path))return null;
        string found=null;
        foreach(var raw in File.ReadAllLines(path)){
            string name=SettingName(raw);
            if(name==null||!string.Equals(name,key,StringComparison.Ordinal))continue;
            string t=raw.Trim(),value=t.Substring(t.IndexOf('=')+1).Trim();
            if(value.Length>=2&&value[0]=='"'&&value[value.Length-1]=='"')value=value.Substring(1,value.Length-2);
            found=value;
        }
        return found;
    }
    static string SettingName(string line){
        string t=line.Trim();
        if(t.Length==0||t[0]=='#')return null;
        int eq=t.IndexOf('=');
        return eq<=0?null:t.Substring(0,eq).Trim();
    }
    public void SavePreference(string key,string value){string path=SettingsPath();if(!File.Exists(path))throw new IOException("Install the game before saving settings.");var edits=new Dictionary<string,string>();edits[key]=value;if(key=="MC3_GRAPHICS_PRESET"&&value!="0"){int n=int.Parse(value);edits["RECOMP_GL_SCALE"]=n==1?"1.5":n==3?"3":"2";edits["RECOMP_GL_FXAA"]="1";edits["RECOMP_GL_ANISO"]=n==1?"4":"16";edits["RECOMP_GL_SHARPEN"]=n==1?"0.25":"0.5";edits["RECOMP_GL_VSYNC"]=n==3?"1":"0";}else if(new[]{"RECOMP_GL_SCALE","RECOMP_GL_FXAA","RECOMP_GL_ANISO","RECOMP_GL_SHARPEN","RECOMP_GL_VSYNC"}.Contains(key))edits["MC3_GRAPHICS_PRESET"]="0";var lines=File.ReadAllLines(path).Where(line=>{string name=SettingName(line);return name==null||!edits.Keys.Any(k=>string.Equals(k,name,StringComparison.Ordinal));}).ToList();foreach(var e in edits)lines.Add(e.Key+"="+e.Value);string backup=path+".launcher-backup";if(!File.Exists(backup))File.Copy(path,backup);string tmp=path+".launcher-tmp";File.WriteAllLines(tmp,lines,new UTF8Encoding(false));File.Replace(tmp,path,null);Append("Saved "+key+". Applies next launch.");}
    /* ---- Online: the shared page, hosted in this window ----------------- */
    WebBrowser onlineView;
    OnlineShell onlineShell;
    string pendingNotice;
    int onlinePolling,onlineBestRoom=-1,onlineBestPlayers;
    DateTime onlineNextPoll=DateTime.MinValue;
    static int InviteRoom=-1;
    static bool ShowOnlinePage;
    static bool InviteOnStart;
    static int JoinOnStart=-1;

    string OnlineHost(){string h=ReadSetting("MC3_ROAM_JOIN","");return h.Length>0?h:"162.35.56.66";}
    int OnlinePort(){int p;if(int.TryParse(ReadSetting("MC3_ROAM_HUB_PORT","47210"),out p)&&p>0&&p<65536)return p;return 47210;}
    int OnlineRoom(){int r;if(int.TryParse(ReadSetting("MC3_ROAM_ROOM","0"),out r)&&r>=0&&r<=255)return r;return 0;}
    /* What other players actually see: the game falls back to MC3_PLAYER_NAME
     * and then "Player", so the page must show the same thing, not blank. */
    string PlayerName(){string n=ReadSetting("MC3_ROAM_NAME","");if(n.Length==0)n=ReadSetting("MC3_PLAYER_NAME","");return n.Length>0?n:"Player";}

    static string OnlineQuery(string host,int port,string query,int timeoutMs){
        using(var u=new UdpClient()){
            u.Client.ReceiveTimeout=timeoutMs;
            byte[] send=Encoding.ASCII.GetBytes(query);
            u.Send(send,send.Length,host,port);
            IPEndPoint from=new IPEndPoint(IPAddress.Any,0);
            byte[] data=u.Receive(ref from);
            return Encoding.ASCII.GetString(data);
        }
    }
    static string OnlineStatusName(int st){
        switch(st){case 0:return "in menus";case 1:return "loading";case 2:return "on the road";
                   case 3:return "in an event";case 4:return "in a shop";case 5:return "paused";default:return "online";}
    }

    void ShowOnline(){
        /* No native heading: the shared page brings its own, and the rail is
         * already the launcher's header. */
        Page("Online");
        Panel host=new Panel {Dock=DockStyle.Fill,BackColor=Bg,Padding=new Padding(18)};
        content.Controls.Add(host);
        onlineView=new WebBrowser();
        onlineView.ScriptErrorsSuppressed=true;
        onlineView.AllowWebBrowserDrop=false;
        onlineView.IsWebBrowserContextMenuEnabled=false;
        onlineView.WebBrowserShortcutsEnabled=false;
        onlineView.Dock=DockStyle.Fill;
        onlineShell=new OnlineShell(this);
        onlineView.ObjectForScripting=onlineShell;
        /* A notice raised before the page finishes loading would be lost:
         * keep it and show it as soon as the document completes. */
        onlineView.DocumentCompleted+=(s,e)=>{ if(pendingNotice!=null){string t=pendingNotice;pendingNotice=null;PushNotice(t);} };
        string pagePath=Path.Combine(root,@"ui\online.html");
        if(File.Exists(pagePath))onlineView.Url=new Uri(pagePath);
        else onlineView.DocumentText="<html><body style='background:#080D1A;color:#EBEFF7;font-family:Segoe UI'><p>ui\\online.html is missing from this install.</p></body></html>";
        host.Controls.Add(onlineView);
        onlineNextPoll=DateTime.MinValue;
        RefreshOnline(true);
    }

    void RefreshOnline(bool force){
        if(page!="Online"||onlineView==null)return;
        if(!force&&DateTime.UtcNow<onlineNextPoll)return;
        onlineNextPoll=DateTime.UtcNow.AddSeconds(3);
        if(Interlocked.Exchange(ref onlinePolling,1)==1)return;
        string host=OnlineHost();int port=OnlinePort();
        Thread t=new Thread(()=>{
            string rooms=null,roster=null,error=null;int ms=-1;
            try{Stopwatch sw=Stopwatch.StartNew();rooms=OnlineQuery(host,port,"MC3Q",600);sw.Stop();ms=(int)sw.ElapsedMilliseconds;}
            catch(Exception x){error=x.Message;}
            if(error==null){try{roster=OnlineQuery(host,port,"MC3P",600);}catch{}}
            try{BeginInvoke(new Action(()=>ApplyOnline(rooms,roster,error,ms,host,port)));}catch{}
            Interlocked.Exchange(ref onlinePolling,0);
        });
        t.IsBackground=true;t.Start();
    }

    void ApplyOnline(string rooms,string roster,string error,int ms,string host,int port){
        if(page!="Online")return;
        int mine=OnlineRoom();
        onlineBestRoom=-1;onlineBestPlayers=0;
        StringBuilder rj=new StringBuilder("[");
        bool firstRoom=true;
        if(rooms!=null&&rooms.StartsWith("MC3ROOMS")){
            string[] parts=rooms.Split(' ');
            for(int i=3;i<parts.Length;i++){
                string[] kv=parts[i].Split(':');
                int g,n;
                if(kv.Length==2&&int.TryParse(kv[0],out g)&&int.TryParse(kv[1],out n)){
                    if(n>onlineBestPlayers&&n<8){onlineBestPlayers=n;onlineBestRoom=g;}
                    if(!firstRoom)rj.Append(',');
                    firstRoom=false;
                    rj.Append("{\"n\":").Append(g).Append(",\"players\":").Append(n).Append('}');
                }
            }
        }
        rj.Append(']');
        StringBuilder pj=new StringBuilder("[");
        bool firstPlayer=true;
        if(roster!=null&&roster.StartsWith("MC3PLAYERS")){
            string[] lines=roster.Replace("\r","").Split('\n');
            for(int i=1;i<lines.Length;i++){
                string[] f=lines[i].Split('|');
                int st,rm;
                if(f.Length>=3&&int.TryParse(f[0],out rm)&&int.TryParse(f[1],out st)){
                    if(!firstPlayer)pj.Append(',');
                    firstPlayer=false;
                    pj.Append("{\"room\":").Append(rm).Append(",\"status\":").Append(st)
                      .Append(",\"name\":").Append(Json(f[2])).Append('}');
                }
            }
        }
        pj.Append(']');
        StringBuilder s=new StringBuilder("{");
        s.Append("\"ms\":").Append(ms);
        s.Append(",\"yourRoom\":").Append(mine);
        s.Append(",\"playerName\":").Append(Json(PlayerName()));
        s.Append(",\"rooms\":").Append(rj);
        s.Append(",\"roster\":").Append(pj);
        s.Append(",\"rosterOk\":").Append(roster!=null&&roster.StartsWith("MC3PLAYERS")?"true":"false");
        if(error!=null)s.Append(",\"error\":").Append(Json(error));
        s.Append('}');
        PushOnline(s.ToString());
    }

    /* The page is the UI; this window just answers it. InvokeScript carries a
     * JSON string (IE passes strings, never objects). */
    void PushOnline(string json){
        if(page!="Online"||onlineView==null)return;
        try{
            if(onlineView.Document==null||onlineView.ReadyState!=WebBrowserReadyState.Complete)return;
            onlineView.Document.InvokeScript("redubApply",new object[]{json});
        }catch{}
    }
    void PushNotice(string text){
        pendingNotice=text;
        if(page!="Online"||onlineView==null)return;
        try{
            if(onlineView.Document==null||onlineView.ReadyState!=WebBrowserReadyState.Complete)return;
            onlineView.Document.InvokeScript("redubNotice",new object[]{text});
            pendingNotice=null;
        }catch{}
    }
    internal void OnlineAction(string action,string json){
        OnlineLog("action "+action+" "+json);
        try{
            if(action=="refresh"){RefreshOnline(true);return;}
            if(action=="join"){
                int room=JsonRoom(json);
                OnlineLog("join parsed room="+room);
                if(room<0)MessageBox.Show(this,"Paste the invite link or type the room number (0-255).","Join a friend");
                else JoinRoom(room);
                return;
            }
            if(action=="invite"){InviteFriends();return;}
            if(action=="name"){
                string who=JsonName(json);
                if(!NameOk(who)){PushNotice("Names use 1-24 letters, numbers, spaces, _ - or .");MessageBox.Show(this,"Names use 1-24 letters, numbers, spaces, _ - or .","Your name");return;}
                try{SavePreference("MC3_ROAM_NAME",who);}
                catch(Exception x){OnlineLog("name failed: "+x);PushNotice("Could not save the name: "+x.Message);return;}
                OnlineLog("name saved: "+who);
                PushNotice("Your name is "+who+".\r\nThe game uses it when it starts.");
                RefreshOnline(true);
                return;
            }
        }catch(Exception x){OnlineLog("action failed: "+x);Append("Online: "+x.Message);}
    }
    /* A file, because the footer is behind the page: when a button "does
     * nothing", this says whether the page called the shell at all. */
    void OnlineLog(string text){
        try{
            string dir=Path.Combine(root,@"work-remix\logs");
            Directory.CreateDirectory(dir);
            File.AppendAllText(Path.Combine(dir,"launcher-online.log"),
                DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss ")+text+Environment.NewLine);
        }catch{}
    }
    /* The name typed on the Online page. The page sends {"name":"..."}; the
     * shell repeats the game's own rule (mc3_nickname_normalize) so a bad name
     * is refused here instead of reaching the relay. */
    static string JsonName(string json){
        if(json==null)return "";
        int i=json.IndexOf("\"name\"");
        if(i<0)return "";
        int j=json.IndexOf(':',i+6);
        if(j<0)return "";
        i=json.IndexOf('"',j+1);
        if(i<0)return "";
        StringBuilder b=new StringBuilder();
        for(int k=i+1;k<json.Length;k++){
            char ch=json[k];
            if(ch=='"')break;
            if(ch=='\\'&&k+1<json.Length){
                char e=json[++k];
                b.Append(e=='n'?'\n':e=='r'?'\r':e=='t'?'\t':e);
                continue;
            }
            b.Append(ch);
        }
        return b.ToString().Trim();
    }
    static bool NameOk(string name){
        if(name==null)return false;
        if(name.Length<1||name.Length>24)return false;
        foreach(char ch in name){
            bool ok=(ch>='a'&&ch<='z')||(ch>='A'&&ch<='Z')||(ch>='0'&&ch<='9')||
                    ch==' '||ch=='_'||ch=='-'||ch=='.';
            if(!ok)return false;
        }
        return true;
    }
    static int JsonRoom(string json){
        if(json==null)return -1;
        int i=json.IndexOf("\"room\"");
        if(i<0)return -1;
        int j=json.IndexOf(':',i+6);
        if(j<0)return -1;
        while(j+1<json.Length&&(json[j+1]==' '||json[j+1]=='\t'))j++;
        int n=0;bool any=false;
        for(int k=j+1;k<json.Length&&json[k]>='0'&&json[k]<='9';k++){n=n*10+(json[k]-'0');any=true;}
        return any&&n>=0&&n<=255?n:-1;
    }
    static string Json(string s){
        if(s==null)return "null";
        StringBuilder b=new StringBuilder("\"");
        for(int i=0;i<s.Length;i++){
            char ch=s[i];
            if(ch=='\"'||ch=='\\')b.Append('\\').Append(ch);
            else if(ch=='\n')b.Append("\\n");
            else if(ch=='\r')b.Append("\\r");
            else if(ch=='\t')b.Append("\\t");
            else if(ch<32)b.Append("\\u").Append(((int)ch).ToString("x4"));
            else b.Append(ch);
        }
        return b.Append('"').ToString();
    }

    void JoinRoom(int room){
        if(room<0||room>255)return;
        OnlineLog("JoinRoom "+room+" running="+GameRunning()+" ready="+Ready());
        if(GameRunning()){
            PushNotice("Room "+room+" is saved.\r\nThe game is running and reads the room only at startup: close it and press Play to join room "+room+".");
            MessageBox.Show(this,"Close the game first: it reads the room number when it starts.","Game is running");
            return;
        }
        try{
            SavePreference("MC3_ROAM_ROOM",room.ToString());
            OnlineLog("room saved, starting the game");
            Append("Room "+room+" saved. Starting the game.");
            PushNotice("Room "+room+" saved. Starting the game...\r\nFriends who join room "+room+" meet you there.");
            PlayGame();
            OnlineLog("PlayGame returned, running="+GameRunning());
            RefreshOnline(true);
        }catch(Exception x){
            OnlineLog("join failed: "+x);
            PushNotice("Could not join room "+room+": "+x.Message);
            MessageBox.Show(this,x.Message,"Join a friend");
        }
    }
    void InviteFriends(){
        int room=OnlineRoom();
        if(room==0){
            /* The shared room is fine for "just play", but an invite needs a
             * number both sides keep; pick a free one and say so. */
            room=PickPrivateRoom();
            if(!GameRunning()){
                try{SavePreference("MC3_ROAM_ROOM",room.ToString());}catch(Exception x){MessageBox.Show(this,x.Message,"Invite friends");return;}
            }
        }
        string link="redub://room/"+room.ToString();
        string text="ReDUB - "+PlayerName()+" invites you to room "+room.ToString()+"\r\n"+link+"\r\n(In ReDUB: Online > Join a friend > "+room.ToString()+")";
        bool copied=false;
        try{Clipboard.SetText(text);copied=true;Append("Invite copied for room "+room.ToString()+".");}
        catch(Exception x){Append("Clipboard failed: "+x.Message);}
        PushNotice((copied?"Invite copied":"Could not copy the invite - here it is")+" for room "+room.ToString()+"\r\n\r\n"+text);
        if(GameRunning())MessageBox.Show(this,"Room "+room.ToString()+" is saved. Close and reopen the game so it joins that room, then send the invite.","Invite friends");
        RefreshOnline(true);
    }
    int PickPrivateRoom(){
        HashSet<int> full=new HashSet<int>();
        string rooms=null;
        try{rooms=OnlineQuery(OnlineHost(),OnlinePort(),"MC3Q",400);}catch{}
        if(rooms!=null&&rooms.StartsWith("MC3ROOMS")){
            string[] parts=rooms.Split(' ');
            for(int i=3;i<parts.Length;i++){
                string[] kv=parts[i].Split(':');
                int g,n;
                if(kv.Length==2&&int.TryParse(kv[0],out g)&&int.TryParse(kv[1],out n)&&n>=8)full.Add(g);
            }
        }
        int start=new Random().Next(1,250);
        for(int k=0;k<255;k++){
            int r=((start+k)%255)+1;
            if(!full.Contains(r))return r;
        }
        return 1;
    }
    static int ParseInvite(string text){
        if(text==null)return -1;
        string s=text.Trim();
        int i=s.IndexOf("room/",StringComparison.OrdinalIgnoreCase);
        if(i>=0)s=s.Substring(i+5);
        int room;
        if(int.TryParse(s,out room)&&room>=0&&room<=255)return room;
        return -1;
    }
    static void RegisterInviteScheme(){
        try{
            string exe=Application.ExecutablePath;
            using(var k=Microsoft.Win32.Registry.CurrentUser.CreateSubKey(@"Software\Classes\redub")){
                if(k==null)return;
                k.SetValue("","URL:ReDUB invite");
                k.SetValue("URL Protocol","");
                using(var c=k.CreateSubKey(@"shell\open\command"))if(c!=null)c.SetValue("","\""+exe+"\" \"%1\"");
            }
        }catch{}
        /* The WebBrowser control starts in an ancient document mode unless the
         * host says otherwise; one per-user value puts the shared page in
         * IE11-edge so its CSS and JSON behave like everywhere else. */
        try{
            using(var k=Microsoft.Win32.Registry.CurrentUser.CreateSubKey(@"Software\Microsoft\Internet Explorer\Main\FeatureControl\FEATURE_BROWSER_EMULATION")){
                if(k!=null)k.SetValue(Path.GetFileName(Application.ExecutablePath),11001,Microsoft.Win32.RegistryValueKind.DWord);
            }
        }catch{}
    }

    void ShowSupport(){var p=Standard("Support","Support & activity","Local setup output. Your game and saves are never uploaded.");ButtonAt(p,"Open log folder",36,140,190,()=>Open("work-remix/logs"));ButtonAt(p,"Check setup",240,140,190,()=>VerifySetup());ButtonAt(p,"Read guide",444,140,190,()=>Open("README.md"));ButtonAt(p,"Report a problem",648,140,190,()=>ReportProblem());log.SetBounds(36,205,Math.Max(600,content.Width-72),Math.Max(280,content.Height-245));log.Anchor=AnchorStyles.Top|AnchorStyles.Left|AnchorStyles.Right|AnchorStyles.Bottom;p.Controls.Add(log);}
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
        else{string edition,error;if(!Xdvdfs.TryVerify(game,out edition,out error))problems.Add(error);else{string data=Xdvdfs.CheckArchives(game);if(data!=null)problems.Add("Game data is incomplete ("+data+"). Install the disc again.");}}
        string dist=RendererDist(root),exe=Path.Combine(dist,"mcdub3remix.exe"),sdl=Path.Combine(dist,"SDL2.dll");
        if(!File.Exists(exe))problems.Add("Game executable is missing: "+exe+". Use the full ReDUB Windows zip, or Settings → Advanced → Rebuild from source.");
        else if(!File.Exists(sdl))problems.Add("Controller runtime is missing: "+sdl+". Use the full ReDUB Windows zip, or Settings → Advanced → Rebuild from source.");
        return problems.ToArray();
    }
    /* Import is copy-only: an extracted folder is copied and an ISO is read by
     * the in-house XDVDFS reader. Both land in a staging folder, the XBE is
     * verified, and only then is the old game-remix replaced. */
    public static string ImportGameFiles(string root,string source,bool replaceExisting,Action<long,long,string> progress,CancellationToken cancellationToken){
        /* A running game holds the disc's files and its saves open; replacing
         * them under it left a half-copied installation and, before the save
         * carry-over existed, no career. Checked here rather than in the
         * window so every caller gets it -- the CLI importer included. */
        if(GameIsRunning())throw new IOException("The game is running. Save and close the game before adding or replacing game files.");
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
            CarryPlayerState(game,staging,progress,cancellationToken);
            cancellationToken.ThrowIfCancellationRequested();
            string old=null;
            if(Directory.Exists(game)){old=game+".old-"+DateTime.Now.ToString("yyyyMMdd-HHmmss-fff");MoveDirectory(game,old);}
            try{MoveDirectory(staging,game);}
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

    /* A directory a just-finished child process, a virus scanner or a backup
     * tool still holds open cannot be moved or deleted for a moment; the retry
     * is for that, not for a genuine failure, which is still reported. A
     * test that fails once and passes on a rerun is how a real regression gets
     * ignored. */
    static void MoveDirectory(string from,string to){
        for(int attempt=0;;attempt++){
            try{Directory.Move(from,to);return;}
            catch(Exception x){
                if(attempt>=8||!(x is IOException||x is UnauthorizedAccessException))throw;
                Thread.Sleep(150);
            }
        }
    }

    /* Whether the game is up, without holding on to its process handles. */
    public static bool GameIsRunning(){
        var all=Process.GetProcessesByName("mcdub3remix");
        foreach(var p in all)p.Dispose();
        return all.Length>0;
    }

    /* A player's career and title settings live inside the game folder, and
     * importing a disc replaces that folder: without this, replacing the game
     * files silently took the career with them (found by
     * tools/windows/Test-Stability.ps1, 2026-11-25).
     *
     * UDATA (profiles, career, garage) and TDATA (title settings) come from
     * the installation, never from the disc -- a disc that happens to carry
     * them must not overwrite a player's own. The copy happens into the staged
     * folder before anything is moved, so a save that cannot be read (the game
     * is running, a backup tool holds it) aborts the import with the old
     * installation still in place. Xbox names are matched without case: the
     * title writes UDATA, the disc and older installs can hold uDaTa, and two
     * folders differing only in case is how a save becomes invisible. */
    static void CarryPlayerState(string oldGame,string staged,Action<long,long,string> progress,CancellationToken cancellationToken){
        if(!Directory.Exists(oldGame))return;
        foreach(string name in new[]{"UDATA","TDATA"}){
            string from=null;
            foreach(string dir in Directory.GetDirectories(oldGame))
                if(string.Equals(Path.GetFileName(dir),name,StringComparison.OrdinalIgnoreCase)){from=dir;break;}
            if(from==null)continue;
            foreach(string dir in Directory.GetDirectories(staged))
                if(string.Equals(Path.GetFileName(dir),name,StringComparison.OrdinalIgnoreCase)){Directory.Delete(dir,true);break;}
            CopyFolder(from,Path.Combine(staged,name),progress,cancellationToken);
        }
    }
    public static ProcessStartInfo LaunchInfo(string root){
        
        string dist=RendererDist(root);
        return new ProcessStartInfo(Path.Combine(dist,"mcdub3remix.exe")){WorkingDirectory=dist,UseShellExecute=true};
    }
    static void StartDirect(string root){
        var running=Process.GetProcessesByName("mcdub3remix");bool active=running.Length>0;foreach(var p in running)p.Dispose();if(active)return;
        if(!InstallationReady(root)){string[] problems=InstallationProblems(root);throw new IOException(problems.Length>0?problems[0]:"The game is not ready. Choose Install.");}
        /* The game looks for game/default.xbe beside its exe unless it is told
         * where the disc is, and Install puts the disc in <root>\game-remix.
         * Without this line a fresh installation found nothing to load. Written
         * once, and left alone if the player set their own. */
        EnsureGameDir(root);
        /* The game is a window program: started on its own its output goes
         * nowhere, so a crash report sent from RecompLauncher arrives with no
         * log. cmd holds the redirect to work-remix/logs/latest.log, and stays
         * alive after this launcher exits, so the file outlives us. */
        string dist=RendererDist(root),log=RunLogPath(root);
        try{if(File.Exists(log))File.Copy(log,log+".previous",true);}catch{}
        var psi=new ProcessStartInfo(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System),"cmd.exe")){
            Arguments="/s /c \"\""+Path.Combine(dist,"mcdub3remix.exe")+"\" > \""+log+"\" 2>&1\"",
            WorkingDirectory=dist,UseShellExecute=false,CreateNoWindow=true};
        var game=Process.Start(psi);
        /* cmd passes the game's own exit code through, and that is the only
         * signal a crash leaves: the log cannot say whether a run was cut
         * short on purpose or died. The handler lives as long as this launcher
         * does (Play keeps it open), which covers the case that matters. */
        try{game.EnableRaisingEvents=true;game.Exited+=(s,e)=>{int code;try{code=game.ExitCode;}catch{return;}RecordExit(root,code);};}catch{}
    }
    static string RunLogPath(string root){string dir=Path.Combine(root,@"work-remix\logs");Directory.CreateDirectory(dir);return Path.Combine(dir,"latest.log");}


    /* Write MC3_GAME_DIR=<root>\game-remix into the settings file if it is not
     * there yet. A path the player set is kept as it is, even if it is wrong:
     * silently rewriting it would hide a moved installation instead of saying
     * so. */
    static void EnsureGameDir(string root){
        try{
            string dir=RendererDist(root),path=Path.Combine(dir,"mcdub3remix_env.txt");
            if(!Directory.Exists(dir))return;
            if(!File.Exists(path)){
                string example=Path.Combine(dir,"mcdub3remix_env.example.txt");
                if(File.Exists(example))File.Copy(example,path);
            }
            if(!File.Exists(path))return;
            foreach(var raw in File.ReadAllLines(path)){
                string line=raw.Trim();
                if(line.StartsWith("MC3_GAME_DIR=",StringComparison.Ordinal)&&line.Length>13)
                    return;
            }
            var lines=new List<string>(File.ReadAllLines(path));
            lines.Add("MC3_GAME_DIR="+Path.Combine(root,"game-remix"));
            string tmp=path+".launcher-tmp";
            File.WriteAllLines(tmp,lines,new UTF8Encoding(false));
            File.Replace(tmp,path,null);
        }catch{}
    }

    /* Crash reporting. mc3port's lesson, kept small: a report is worth having
     * only if it arrives with the log of the run that failed, and asking the
     * player to find that file is where reports die. The exit code of the last
     * run is stamped on disk; a clean exit clears the stamp. The issue is
     * prefilled, the zip is written here, and nothing is sent by itself. */
    static string CrashMarker(string root){return Path.Combine(root,@"work-remix\logs\last-exit.txt");}
    static void RecordExit(string root,int code){
        try{
            string m=CrashMarker(root);
            if(code==0){if(File.Exists(m))File.Delete(m);return;}
            File.WriteAllText(m,code.ToString()+"\t"+DateTime.UtcNow.ToString("yyyy-MM-ddTHH:mm:ssZ"),new UTF8Encoding(false));
        }catch{}
    }
    /* The description of an unacknowledged crash, or null. */
    static string LastCrash(string root){
        try{
            string m=CrashMarker(root);if(!File.Exists(m))return null;
            string[] f=File.ReadAllText(m).Split('\t');if(f.Length<2)return null;
            return "exit "+f[0]+" at "+f[1];
        }catch{return null;}
    }
    static string LauncherVersion(){try{return Assembly.GetExecutingAssembly().GetName().Version.ToString(3);}catch{return "unknown";}}
    /* The last lines of a log that can be hundreds of megabytes: read a window
     * from the end, drop the partial first line, keep the tail asked for. */
    static string TailText(string path,int lines){
        try{
            using(var fs=new FileStream(path,FileMode.Open,FileAccess.Read,FileShare.ReadWrite)){
                long n=Math.Min(fs.Length,131072);if(n<=0)return "";
                fs.Seek(-n,SeekOrigin.End);var buf=new byte[(int)n];int read=0;
                while(read<(int)n){int got=fs.Read(buf,read,(int)n-read);if(got<=0)break;read+=got;}
                string t=new UTF8Encoding(false,false).GetString(buf,0,read);
                int cut=t.IndexOf('\n');if(n<fs.Length&&cut>=0)t=t.Substring(cut+1);
                string[] all=t.Replace("\r","").Split('\n');
                int end=all.Length;if(end>0&&all[end-1].Length==0)end--;
                int start=Math.Max(0,end-lines);
                return string.Join("\n",all.Skip(start).Take(end-start));
            }
        }catch{return "";}
    }
    static string MakeCrashZip(string root){
        string logs=Path.Combine(root,@"work-remix\logs");if(!Directory.Exists(logs))return null;
        string zip=Path.Combine(root,"ReDUB-crash-report.zip");
        try{if(File.Exists(zip))File.Delete(zip);}catch{}
        try{
            using(var z=new ZipArchive(new FileStream(zip,FileMode.Create),ZipArchiveMode.Create)){
                foreach(string f in Directory.GetFiles(logs,"*.log*")){
                    var e=z.CreateEntry(Path.GetFileName(f),CompressionLevel.Optimal);
                    using(var s=e.Open())using(var i=new FileStream(f,FileMode.Open,FileAccess.Read,FileShare.ReadWrite))i.CopyTo(s);
                }
            }
        }catch{return null;}
        return zip;
    }
    static string NewIssueUrl(string title,string body){
        return "https://github.com/dea7hsmiless/Midnight-Club-3-ReDUB/issues/new?labels=crash&title="+Uri.EscapeDataString(title)+"&body="+Uri.EscapeDataString(body);
    }
    string ReportBody(string root,string crash){
        string log=TailText(RunLogPath(root),40);
        return "**What happened**\n\n(one line: what you were doing when it went wrong)\n\n**Build**\n\n- ReDUB launcher "+LauncherVersion()+"\n- Windows "+Environment.OSVersion.Version+" ("+(Environment.Is64BitOperatingSystem?"x64":"x86")+")\n- Source of this copy: "+(IsSourceRoot(root)?"git checkout":"release package")+"\n- Last run: "+crash+"\n\n**Last 40 log lines**\n\n```\n"+log+"\n```\n\nA `ReDUB-crash-report.zip` with the full logs was written next to the launcher; attach it here.\n";
    }
    void OpenUrl(string url){try{Process.Start(new ProcessStartInfo(url){UseShellExecute=true});}catch(Exception x){MessageBox.Show(this,"Could not open the browser: "+x.Message,"ReDUB");}}
    void ReportCrash(string crash){
        string zip=MakeCrashZip(root);
        OpenUrl(NewIssueUrl("Crash: last run "+crash,ReportBody(root,crash)));
        if(zip!=null){
            try{Clipboard.SetText(zip);}catch{}
            MessageBox.Show(this,"The report form is open in your browser.\n\nThe crash report was written to:\n"+zip+"\n\nThat path is on the clipboard. Attach the file to the issue.","ReDUB");
        }else MessageBox.Show(this,"The report form is open in your browser.\n\nThe crash report could not be written; attach work-remix\\logs\\latest.log instead.","ReDUB");
    }
    void ReportProblem(){
        OpenUrl(NewIssueUrl("ReDUB: ",ReportBody(root,"no crash recorded")));
    }
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
        if(args.Length>0&&args[0]=="--self-test"){
    if(Quote(@"D:\folder with spaces\")!="\"D:\\folder with spaces\\\\\"")Environment.Exit(2);
    if(!File.Exists(Path.Combine(root,@".portable\Build.ps1")))Environment.Exit(3);
    /* The Online page's name field: JSON parsing and the game's own rule. */
    if(JsonName("{\"name\":\"Racer 1\"}")!="Racer 1")Environment.Exit(4);
    if(JsonName("{\"name\":\"a\\\"b\"}")!="a\"b")Environment.Exit(5);
    if(!NameOk("Racer 1")||NameOk("")||NameOk("bad!name")||NameOk(new string('x',25)))Environment.Exit(6);
    Directory.CreateDirectory(Path.Combine(root,@"work-remix\logs"));
    File.WriteAllText(Path.Combine(root,@"work-remix\logs\launcher-self-test.log"),"PASS: project discovery, argument quoting, name parsing and validation");
    return;
}        if(args.Length>0){string command=args[0].ToLowerInvariant();if(command=="--import"||command=="--play"||command=="--check"||command=="--build")Environment.Exit(RunCommand(root,command,args));}
        RegisterInviteScheme();
        if(args.Length>0)InviteRoom=ParseInvite(args[0]);
        if(args.Length>0&&args[0].ToLowerInvariant()=="--online")ShowOnlinePage=true;
        if(args.Length>0&&args[0].ToLowerInvariant()=="--invite"){ShowOnlinePage=true;InviteOnStart=true;}
        if(args.Length>1&&args[0].ToLowerInvariant()=="--join"){int r;if(int.TryParse(args[1],out r)&&r>=0&&r<=255){ShowOnlinePage=true;JoinOnStart=r;}}
        string startupError=null;
        if(ShouldBypass(BypassEnabled(root),args,(Control.ModifierKeys&Keys.Shift)!=0,InstallationReady(root))){try{StartDirect(root);return;}catch(Exception ex){startupError="Direct launch failed: "+ex.Message;}}
        Application.EnableVisualStyles();Application.SetCompatibleTextRenderingDefault(false);var app=new Launcher(root);if(startupError!=null)app.Append(startupError);Application.Run(app);
    }
}

/* The shared page's only handle on this window: window.external.redubCall(action,
 * json). COM-visible so the WebBrowser control exposes it to script. */
[System.Runtime.InteropServices.ComVisible(true)]
public class OnlineShell {
    readonly Launcher app;
    internal OnlineShell(Launcher owner){app=owner;}
    public void redubCall(string action,string json){app.OnlineAction(action,json);}
}
}


