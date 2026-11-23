#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#import <QuartzCore/QuartzCore.h>
#import "ios_xdvdfs.h"
#include <stdlib.h>
#include <stdio.h>

#ifndef MC3_VERSION
#  define MC3_VERSION "dev"
#endif

static UIColor *RGB(int hex) {return [UIColor colorWithRed:((hex>>16)&255)/255. green:((hex>>8)&255)/255. blue:(hex&255)/255. alpha:1];}
static NSString *Documents(void){return NSSearchPathForDirectoriesInDomains(NSDocumentDirectory,NSUserDomainMask,YES).firstObject;}
static NSString *Game(void){return [Documents() stringByAppendingPathComponent:@"game"];}
static NSString *Env(void){return [Documents() stringByAppendingPathComponent:@"mcdub3remix_env.txt"];}
static BOOL Exists(NSString *p){return [[NSFileManager defaultManager] fileExistsAtPath:p];}
/* Size of a directory's regular files, for the free-space check. */
static unsigned long long DirSize(NSString *dir){unsigned long long n=0;NSFileManager *fm=NSFileManager.defaultManager;NSDirectoryEnumerator *e=[fm enumeratorAtPath:dir];for(NSString *f in e){NSDictionary *a=[fm attributesOfItemAtPath:[dir stringByAppendingPathComponent:f] error:nil];if([a[NSFileType] isEqual:NSFileTypeRegular])n+=[a[NSFileSize] unsignedLongLongValue];}return n;}
/* Game data is 4.4 GB and can be reinstalled from the disc; keep it out of iCloud backups. Saves stay backed up. */
static void ExcludeFromBackup(NSString *p){NSURL *u=[NSURL fileURLWithPath:p];[u setResourceValue:@YES forKey:NSURLIsExcludedFromBackupKey error:nil];}
/* The document picker's asCopy copy lives in our temp/Inbox; after extraction
 * it is a second 4.4 GB file. Never touch the user's original. */
static void CleanupPickedCopy(NSURL *u){NSString *p=u.path;if(!p)return;NSString *tmp=NSTemporaryDirectory();NSString *inbox=[Documents() stringByAppendingPathComponent:@"Inbox/"];if([p hasPrefix:tmp]||[p hasPrefix:inbox])[NSFileManager.defaultManager removeItemAtURL:u error:nil];}
/* A named graphics preset, applied as individual env values so the other rows
 * stay the source of truth (and remain individually changeable). */
static NSDictionary *PresetValues(NSString *name){
 if([name isEqual:@"performance"])return @{@"RECOMP_GL_SCALE":@"1",@"RECOMP_GL_UI_SCALE":@"2",@"RECOMP_GL_ANISO":@"1",@"RECOMP_GL_FXAA":@"0",@"RECOMP_GL_SHARPEN":@"0"};
 if([name isEqual:@"quality"])return @{@"RECOMP_GL_SCALE":@"3",@"RECOMP_GL_UI_SCALE":@"3",@"RECOMP_GL_ANISO":@"16",@"RECOMP_GL_FXAA":@"2",@"RECOMP_GL_SHARPEN":@"0.75"};
 return @{@"RECOMP_GL_SCALE":@"2",@"RECOMP_GL_UI_SCALE":@"3",@"RECOMP_GL_ANISO":@"4",@"RECOMP_GL_FXAA":@"1",@"RECOMP_GL_SHARPEN":@"0.5"};
}
static NSString *BundleGame(void){return [NSBundle.mainBundle.bundlePath stringByAppendingPathComponent:@"game"];}
static UILabel *Text(NSString *s,CGFloat size,BOOL bold){UILabel *v=[UILabel new];v.text=s;v.font=bold?[UIFont boldSystemFontOfSize:size]:[UIFont systemFontOfSize:size];v.textColor=RGB(0xE6EBF5);v.numberOfLines=0;return v;}
/* touch_pad.c: the stick pedals' haptic detents (RECOMP_TOUCH_HAPTICS).
 * UIKit wants the main thread and the engine wants to be prepared, so the
 * tick is queued rather than fired from the render thread. */
void ios_haptic_tick(int strong){
 dispatch_async(dispatch_get_main_queue(),^{
  static UIImpactFeedbackGenerator *light,*medium;
  if(!light){light=[[UIImpactFeedbackGenerator alloc]initWithStyle:UIImpactFeedbackStyleLight];medium=[[UIImpactFeedbackGenerator alloc]initWithStyle:UIImpactFeedbackStyleMedium];}
  UIImpactFeedbackGenerator *g=strong?medium:light;[g prepare];[g impactOccurred];
 });
}

@interface ReDUBLauncher : UIViewController<UIDocumentPickerDelegate>
@property(nonatomic,strong) UIView *rail;
@property(nonatomic,strong) UIScrollView *pane;
@property(nonatomic,strong) UILabel *footer;
@property(nonatomic,strong) UIProgressView *progress;
@property(nonatomic,strong) UILabel *progressText;
@property(nonatomic,strong) NSURL *iso;
@property(nonatomic,strong) NSString *page;
@property(nonatomic,strong) NSString *status;
@property(nonatomic,strong) NSString *activeGame;
@property(nonatomic,strong) NSMutableDictionary *settings;
@property(nonatomic,strong) NSMutableDictionary *valueButtons;
@property(nonatomic,strong) NSTimer *timer;
@property(nonatomic) BOOL done,busy,verified;
@property(nonatomic) UIBackgroundTaskIdentifier backgroundTask;
- (void)acceptISOURL:(NSURL*)url;
@end

static __weak ReDUBLauncher *s_activeLauncher;
static NSURL *s_pendingISOURL;
void ios_launcher_receive_url(NSURL *url) {
 if(!url||![url.pathExtension.lowercaseString isEqual:@"iso"])return;
 s_pendingISOURL=url;
 dispatch_async(dispatch_get_main_queue(),^{
  if(s_activeLauncher)[s_activeLauncher acceptISOURL:url];
 });
}

static void Progress(uint64_t done,uint64_t total,const char *name,void *context){
 ReDUBLauncher *vc=(__bridge ReDUBLauncher*)context;
 static double last;double now=CACurrentMediaTime();if(now-last<0.12 && done<total)return;last=now;
 NSString *s=[NSString stringWithFormat:@"Installing %.0f%% — %@",total?100.*done/total:0,[NSString stringWithUTF8String:name]?:@"file"];
 dispatch_async(dispatch_get_main_queue(),^{vc.status=s;vc.progress.progress=total?(float)((double)done/total):0;vc.progressText.text=s;vc.footer.text=s;});
}

@implementation ReDUBLauncher
- (UIInterfaceOrientationMask)supportedInterfaceOrientations{return UIInterfaceOrientationMaskLandscape;}
- (BOOL)prefersStatusBarHidden{return YES;}
- (void)viewDidLoad {
 [super viewDidLoad];self.view.backgroundColor=RGB(0x080D1A);self.settings=[NSMutableDictionary dictionary];self.valueButtons=[NSMutableDictionary dictionary];self.page=@"Game";self.backgroundTask=UIBackgroundTaskInvalid;
 s_activeLauncher=self;
 [[NSFileManager defaultManager] createDirectoryAtPath:Documents() withIntermediateDirectories:YES attributes:nil error:nil];
 NSString *raw=[NSString stringWithContentsOfFile:Env() encoding:NSUTF8StringEncoding error:nil];
 for(NSString *line in [raw componentsSeparatedByCharactersInSet:NSCharacterSet.newlineCharacterSet]){NSRange r=[line rangeOfString:@"="];if(r.location!=NSNotFound && ![line hasPrefix:@"#"]){NSString *k=[[line substringToIndex:r.location] stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet];self.settings[k]=[line substringFromIndex:r.location+1];}}
 /* Recover interrupted transactional install before exposing Play. */
 NSFileManager *fm=NSFileManager.defaultManager;NSString *backup=[Documents() stringByAppendingPathComponent:@"game.previous"];
 if(!Exists(Game())&&Exists(backup))[fm moveItemAtPath:backup toPath:Game() error:nil];
 self.activeGame=Exists([Game() stringByAppendingPathComponent:@"default.xbe"])?Game():BundleGame();
 if(Exists(Game()))ExcludeFromBackup(Game());
 char error[512];self.verified=ios_disc_verify(self.activeGame.fileSystemRepresentation,error,sizeof error);
 self.status=self.verified?@"Ready to play  /  iOS ARM64":@"Choose your Xbox Remix ISO to begin";
 self.rail=[UIView new];self.rail.backgroundColor=RGB(0x050912);[self.view addSubview:self.rail];
 self.pane=[UIScrollView new];self.pane.alwaysBounceVertical=YES;[self.view addSubview:self.pane];
 self.footer=Text(self.status,11,NO);self.footer.backgroundColor=RGB(0x0E1627);[self.view addSubview:self.footer];
 self.timer=[NSTimer scheduledTimerWithTimeInterval:1.5 target:self selector:@selector(tick) userInfo:nil repeats:YES];
 if(s_pendingISOURL)[self acceptISOURL:s_pendingISOURL];
}
- (void)dealloc {if(s_activeLauncher==self)s_activeLauncher=nil;}
- (void)tick{self.footer.text=self.status;}
- (UIButton*)button:(NSString*)title frame:(CGRect)frame parent:(UIView*)parent action:(SEL)action primary:(BOOL)primary {
 UIButton *b=[UIButton buttonWithType:UIButtonTypeSystem];b.frame=frame;b.backgroundColor=RGB(primary?0xEE3E50:0x0F172A);b.layer.cornerRadius=9;[b setTitle:title forState:UIControlStateNormal];[b setTitleColor:UIColor.whiteColor forState:UIControlStateNormal];b.titleLabel.font=[UIFont boldSystemFontOfSize:14];[b addTarget:self action:action forControlEvents:UIControlEventTouchUpInside];[parent addSubview:b];return b;
}
- (void)viewDidLayoutSubviews {
 [super viewDidLayoutSubviews];UIEdgeInsets safe=self.view.safeAreaInsets;CGFloat w=self.view.bounds.size.width,h=self.view.bounds.size.height,rail=w<800?126:160;
 self.rail.frame=CGRectMake(safe.left,0,rail,h-safe.bottom-25);self.pane.frame=CGRectMake(safe.left+rail, safe.top,w-safe.left-safe.right-rail,h-safe.top-safe.bottom-25);self.footer.frame=CGRectMake(safe.left,h-safe.bottom-25,w-safe.left-safe.right,25);
 [self render];
}
- (void)nav:(UIButton*)sender {if(self.busy)return;self.page=sender.currentTitle;[self render];}
- (void)label:(NSString*)s size:(CGFloat)size bold:(BOOL)bold frame:(CGRect)rect parent:(UIView*)v muted:(BOOL)muted {UILabel *l=Text(s,size,bold);l.frame=rect;if(muted)l.textColor=RGB(0x99A5BC);[v addSubview:l];}
- (void)render {
 for(UIView *v in self.rail.subviews)[v removeFromSuperview];for(UIView *v in self.pane.subviews)[v removeFromSuperview];
 CGFloat rw=self.rail.bounds.size.width,w=self.pane.bounds.size.width,h=self.pane.bounds.size.height;if(w<1)return;
 [self label:@"ReDUB" size:26 bold:YES frame:CGRectMake(16,16,rw-20,42) parent:self.rail muted:NO];
 NSArray *pages=@[@"Game",@"Install",@"Settings",@"Support",@"About"];
 for(NSUInteger i=0;i<pages.count;i++){CGFloat y=i<3?74+i*49:MAX(228,self.rail.bounds.size.height-100)+(i-3)*45;UIButton *b=[self button:pages[i] frame:CGRectMake(9,y,rw-18,41) parent:self.rail action:@selector(nav:) primary:[self.page isEqual:pages[i]]];b.enabled=!self.busy;}
 CGFloat x=22,cw=w-44;
 if([self.page isEqual:@"Game"]){
 CGFloat heroH=MAX(150,MIN(280,h-160));UIView *hero=[[UIView alloc]initWithFrame:CGRectMake(0,0,w,heroH)];hero.clipsToBounds=YES;[self.pane addSubview:hero];
 UIImageView *art=[[UIImageView alloc]initWithImage:[UIImage imageWithContentsOfFile:[NSBundle.mainBundle.bundlePath stringByAppendingPathComponent:@"Assets/hero.jpg"]]];art.frame=hero.bounds;art.contentMode=UIViewContentModeScaleAspectFill;[hero addSubview:art];
 CAGradientLayer *gradient=[CAGradientLayer layer];gradient.frame=hero.bounds;gradient.colors=@[(id)[RGB(0x080D1A) colorWithAlphaComponent:0.12].CGColor,(id)RGB(0x080D1A).CGColor];[hero.layer addSublayer:gradient];
 UIImageView *logo=[[UIImageView alloc]initWithImage:[UIImage imageWithContentsOfFile:[NSBundle.mainBundle.bundlePath stringByAppendingPathComponent:@"Assets/logo.png"]]];logo.frame=CGRectMake(24,20,MIN(300,w*0.62),heroH-34);logo.contentMode=UIViewContentModeScaleAspectFit;[hero addSubview:logo];
 [self label:self.verified?@"INSTALLED  /  iOS ARM64":@"BRING YOUR OWN XBOX DISC" size:11 bold:YES frame:CGRectMake(x,heroH+1,cw,22) parent:self.pane muted:YES];
 [self label:@"Midnight Club 3: ReDUB" size:24 bold:YES frame:CGRectMake(x,heroH+27,cw,34) parent:self.pane muted:NO];
 [self label:@"DUB Edition Remix, rebuilt for iPhone and iPad." size:13 bold:NO frame:CGRectMake(x,heroH+65,cw,25) parent:self.pane muted:YES];
 CGFloat y=heroH+105;[self button:self.verified?@"Play":@"Install" frame:CGRectMake(x,y,MAX(110,cw*.4),48) parent:self.pane action:@selector(play) primary:YES];
 [self button:@"Verify setup" frame:CGRectMake(x+cw*.43,y,cw*.27,48) parent:self.pane action:@selector(verify) primary:NO];
 [self button:@"Game files" frame:CGRectMake(x+cw*.73,y,cw*.27,48) parent:self.pane action:@selector(files) primary:NO];self.pane.contentSize=CGSizeMake(w,y+70);
 } else if([self.page isEqual:@"Install"]){
 [self label:@"Install ReDUB" size:27 bold:YES frame:CGRectMake(x,16,cw,38) parent:self.pane muted:NO];
 [self label:@"Select your original Xbox DUB Edition Remix ISO. PS2 and Switch images are not supported." size:13 bold:NO frame:CGRectMake(x,59,cw,48) parent:self.pane muted:YES];
 [self label:self.iso.lastPathComponent?:@"Choose your game source" size:19 bold:YES frame:CGRectMake(x,116,cw,45) parent:self.pane muted:NO];
 [self button:@"Choose Xbox ISO" frame:CGRectMake(x,175,190,46) parent:self.pane action:@selector(chooseISO) primary:NO].enabled=!self.busy;
 UIButton *install=[self button:self.verified?@"Install / Repair":@"Install game" frame:CGRectMake(x+205,175,MAX(150,cw-205),46) parent:self.pane action:@selector(install) primary:YES];install.enabled=self.iso!=nil&&!self.busy;
 self.progress=[[UIProgressView alloc]initWithProgressViewStyle:UIProgressViewStyleDefault];self.progress.frame=CGRectMake(x,240,cw,8);self.progress.progressTintColor=RGB(0xEE3E50);[self.pane addSubview:self.progress];
 self.progressText=Text(self.status,12,NO);self.progressText.frame=CGRectMake(x,255,cw,50);[self.pane addSubview:self.progressText];
 [self label:@"Installation extracts and verifies your disc on this device. The native runtime is included in the app; no native code compilation occurs here. Allow at least 5 GB free for extracted files. Keep the app open during installation." size:12 bold:NO frame:CGRectMake(x,315,cw,78) parent:self.pane muted:YES];self.pane.contentSize=CGSizeMake(w,420);
 } else if([self.page isEqual:@"Settings"]){[self renderSettings:cw];}
 else {
 BOOL support=[self.page isEqual:@"Support"];
 [self label:support?@"Support":@"About ReDUB" size:27 bold:YES frame:CGRectMake(x,20,cw,42) parent:self.pane muted:NO];
 [self label:support?@"Import a supported Xbox Remix ISO in Install. Pair a controller in iOS Settings. Settings apply on the next game launch. Share your settings and launch log when reporting a problem.":@"Midnight Club 3: ReDUB " MC3_VERSION @"\nOriginal Xbox static recompilation for iOS.\n\nYour own disc is required. The launcher installs game data locally and uses the native runtime bundled with this app." size:15 bold:NO frame:CGRectMake(x,85,cw,180) parent:self.pane muted:YES];
 if(support){[self button:@"Share diagnostics" frame:CGRectMake(x,280,210,46) parent:self.pane action:@selector(share) primary:YES];[self button:@"Open app files" frame:CGRectMake(x+225,280,190,46) parent:self.pane action:@selector(files) primary:NO];}
 self.pane.contentSize=CGSizeMake(w,370);
 }
}
- (NSArray*)rows {return @[
 @[@"Display",@"Gamma",@"MC3_GAMMA",@[@"0.8",@"0.9",@"1",@"1.1",@"1.2",@"1.3",@"1.4"],@"1",@[@"0.8",@"0.9",@"1.0 (Default)",@"1.1",@"1.2",@"1.3",@"1.4"]],
 @[@"Display",@"Brightness",@"MC3_BRIGHTNESS",@[@"0.8",@"0.9",@"1",@"1.1",@"1.2"],@"1",@[@"80%",@"90%",@"100% (Default)",@"110%",@"120%"]],
 @[@"Display",@"VSync",@"RECOMP_GL_VSYNC",@[@"0",@"1",@"-1"],@"1",@[@"Off",@"On",@"Adaptive"]],
 @[@"Graphics",@"Preset",@"MC3_GRAPHICS_PRESET",@[@"default",@"performance",@"quality"],@"default",@[@"Default",@"Performance",@"Quality"],@"Applies a matching set of graphics values; the rows below update to match. Render scale, HUD and filtering apply on the next launch."],
 @[@"Graphics",@"Render scale",@"RECOMP_GL_SCALE",@[@"1",@"1.5",@"2",@"3",@"5"],@"2",@[@"1x",@"1.5x",@"2x",@"3x",@"5x"]],
 @[@"Graphics",@"HUD texture quality",@"RECOMP_GL_UI_SCALE",@[@"1",@"2",@"3",@"5"],@"3",@[@"1x",@"2x",@"3x (Default)",@"5x"]],
 @[@"Graphics",@"Anti-aliasing",@"RECOMP_GL_FXAA",@[@"0",@"1",@"2",@"3"],@"1",@[@"Off",@"FXAA",@"FXAA Low",@"FXAA High"]],
 @[@"Graphics",@"Texture filtering",@"RECOMP_GL_ANISO",@[@"1",@"4",@"16"],@"4",@[@"1x",@"4x (Default)",@"16x"]],
 @[@"Graphics",@"Sharpening",@"RECOMP_GL_SHARPEN",@[@"0",@"0.25",@"0.5",@"0.75",@"1"],@"0.5",@[@"Off",@"25%",@"50%",@"75%",@"100%"]],
 @[@"Graphics",@"Frame queue",@"RECOMP_GL_QUEUE_FRAMES",@[@"1",@"2"],@"2",@[@"1 frame",@"2 frames"]],
 @[@"Graphics",@"Display",@"RECOMP_GL_ASPECT",@[@"auto",@"4:3",@"fill"],@"auto",@[@"Auto (16:9)",@"4:3",@"Stretch"],@"Auto fills widescreen frames to 16:9 (pillarboxed on a tall phone); 4:3 keeps the original shape; Stretch uses every pixel of the screen."],
 @[@"Controls",@"On-screen controls",@"RECOMP_TOUCH_PAD",@[@"-1",@"1",@"0"],@"-1",@[@"Auto",@"Always",@"Off"]],
 @[@"Controls",@"Pad opacity",@"RECOMP_TOUCH_PAD_ALPHA",@[@"0.35",@"0.45",@"0.55",@"0.7",@"0.85",@"1"],@"0.55",@[@"35%",@"45%",@"55% (Default)",@"70%",@"85%",@"100%"]],
 @[@"Controls",@"Pad size",@"RECOMP_TOUCH_PAD_SCALE",@[@"0.8",@"0.9",@"1",@"1.1",@"1.2"],@"1",@[@"80%",@"90%",@"100% (Default)",@"110%",@"120%"]],
 @[@"Controls",@"Pedals",@"RECOMP_TOUCH_PEDALS",@[@"stick",@"triggers",@"bars",@"sliders"],@"stick",@[@"Stick up / down (Default)",@"LT / RT buttons",@"Shoulder bars",@"Edge sliders"]],
 @[@"Controls",@"Tap-to-latch gas",@"RECOMP_TOUCH_LATCH",@[@"0",@"1"],@"0",@[@"Off",@"On"]],
 @[@"Controls",@"Auto-accelerate",@"RECOMP_TOUCH_AUTO_GAS",@[@"0",@"1"],@"0",@[@"Off",@"On"]],
 @[@"Controls",@"Touch haptics",@"RECOMP_TOUCH_HAPTICS",@[@"1",@"0"],@"1",@[@"On",@"Off"],@"A light click when a pedal engages and a firmer one at full throttle, in every pedal layout."],
 @[@"Controls",@"Controller vibration",@"MC3_RUMBLE_STRENGTH",@[@"0",@"50",@"75",@"100",@"125",@"150"],@"100",@[@"Off",@"50%",@"75%",@"100%",@"125%",@"150%"]],
 @[@"Advanced",@"Steering dead zone",@"RECOMP_TOUCH_DEADZONE",@[@"0",@"3",@"5",@"8",@"12"],@"5",@[@"Off",@"3%",@"5% (Default)",@"8%",@"12%"],@"How far the stick must move before it steers. Ignores tiny thumb movements around the centre; full lock is still reached at the ring's edge."],
 @[@"Advanced",@"Throttle ramp",@"RECOMP_TOUCH_RAMP",@[@"0",@"150",@"300"],@"0",@[@"Off",@"Fast (150 ms)",@"Smooth (300 ms)"],@"Eases the touch pedals from rest to full throttle or brake over 150 or 300 ms instead of an instant press. Gives smoother launches and less wheelspin; a real controller's triggers are not affected."],
 @[@"Diagnostics",@"FPS counter",@"MC3_FPS_OVERLAY",@[@"0",@"1"],@"0",@[@"Off",@"On"],@"Draws the frame rate at the top of the screen."],
 @[@"Diagnostics",@"GL error log",@"RECOMP_GL_ERRORS",@[@"0",@"1",@"2"],@"0",@[@"Off",@"Step",@"Call"],@"Step names the stage that raised a GL error; Call names the exact GL call and is slow, for chasing a driver problem."],
 @[@"Diagnostics",@"Renderer trace",@"RECOMP_GL_TRACE",@[@"0",@"1"],@"0",@[@"Off",@"On"],@"Writes shader sources, surface and texture creation, and any texture mode the renderer does not implement (bump, dot, cube) to the launch log. Diagnostic only: it slows loading and makes a large log. Turn it off when done."],
 @[@"Diagnostics",@"Texgen trace",@"RECOMP_GL_TEXGEN_TRACE",@[@"0",@"1"],@"0",@[@"Off",@"On"],@"Logs fixed-function texture-coordinate generation that the renderer cannot yet do. Use with Renderer trace when chasing graphics bugs in the garage or menus."],
 @[@"Diagnostics",@"Feedback trace",@"RECOMP_GL_FEEDBACK_TRACE",@[@"0",@"1"],@"0",@[@"Off",@"On"],@"Logs draws that sample the surface they are drawing into, which GL does not define. Changes nothing on screen."]];}
- (NSString*)settingLabel:(NSArray*)row value:(NSString*)value {
 NSUInteger index=[row[3] indexOfObject:value];
 return index==NSNotFound?value:row[5][index];
}
- (void)renderSettings:(CGFloat)cw {
 [self label:@"Settings" size:27 bold:YES frame:CGRectMake(22,16,cw,42) parent:self.pane muted:NO];[self label:@"Tap a value or an arrow to change it. Saved for the next launch." size:12 bold:NO frame:CGRectMake(22,61,cw,28) parent:self.pane muted:YES];
 self.valueButtons=[NSMutableDictionary dictionary];
 CGFloat y=106;NSString *category=@"";NSUInteger i=0;
 for(NSArray *r in self.rows){
  if(![category isEqual:r[0]]){
   category=r[0];[self label:category size:18 bold:YES frame:CGRectMake(22,y,cw,28) parent:self.pane muted:NO];y+=36;
   /* Say plainly that this section is optional tuning, not required setup. */
   if([category isEqual:@"Advanced"]){[self label:@"Fine-tuning. The defaults are safe to keep; change these only if you want to." size:11 bold:NO frame:CGRectMake(30,y,cw-60,24) parent:self.pane muted:YES];y+=26;}
  }
  BOOL many=[r[3] count]>1;
  /* Arrows on both sides of every multi-value setting; one value button on
   * its own for a plain on/off would still work, but the arrows make the
   * direction explicit and stop a stray tap from changing something. */
  CGFloat labelW=cw-(many?238.0:162.0);if(labelW<110.0)labelW=110.0;
  [self label:r[1] size:13 bold:NO frame:CGRectMake(30,y,labelW,42) parent:self.pane muted:NO];
  NSString *value=self.settings[r[2]]?:r[4];
  CGFloat vx=cw+(many?-164.0:-126.0);
  UIButton *vb=[self button:[self settingLabel:r value:value] frame:CGRectMake(vx,y,140,42) parent:self.pane action:@selector(nextSetting:) primary:NO];vb.tag=i;
  vb.accessibilityLabel=[NSString stringWithFormat:@"%@, %@",r[1],[self settingLabel:r value:value]];
  if(r.count>6&&[r[6] length])vb.accessibilityHint=r[6];
  self.valueButtons[@(i)]=vb;
  if(many){
   UIButton *rt=[self button:@"▶" frame:CGRectMake(vx+144,y,34,42) parent:self.pane action:@selector(nextSetting:) primary:NO];rt.tag=i;
   UIButton *lt=[self button:@"◀" frame:CGRectMake(vx-38,y,34,42) parent:self.pane action:@selector(prevSetting:) primary:NO];lt.tag=i;
   rt.accessibilityLabel=[NSString stringWithFormat:@"Next %@",r[1]];
   lt.accessibilityLabel=[NSString stringWithFormat:@"Previous %@",r[1]];
  }
  i++;y+=53;
  /* Optional seventh element: what the setting actually does. */
  if(r.count>6&&[r[6] length]){[self label:r[6] size:11 bold:NO frame:CGRectMake(30,y,MAX(160,cw-60),44) parent:self.pane muted:YES];y+=44;}
 }self.pane.contentSize=CGSizeMake(self.pane.bounds.size.width,y+20);
}
- (void)prevSetting:(UIButton*)b {[self shiftRow:b.tag delta:-1];}
- (void)nextSetting:(UIButton*)b {[self shiftRow:b.tag delta:1];}
- (void)refreshValueButtons {
 for(NSNumber *key in self.valueButtons){NSUInteger i=key.unsignedIntegerValue;if(i>=self.rows.count)continue;
  NSArray *row=self.rows[i];NSString *v=self.settings[row[2]]?:row[4];
  UIButton *b=self.valueButtons[key];NSString *label=[self settingLabel:row value:v];
  [b setTitle:label forState:UIControlStateNormal];
  b.accessibilityLabel=[NSString stringWithFormat:@"%@, %@",row[1],label];
 }
}
- (void)shiftRow:(NSUInteger)i delta:(NSInteger)d {
 NSArray *r=self.rows[i],*values=r[3];NSString *current=self.settings[r[2]]?:r[4];
 NSInteger index=[values indexOfObject:current];if(index==NSNotFound)index=0;
 NSInteger count=(NSInteger)values.count;if(count<1)return;
 NSString *next=values[(index+d+count)%count];
 NSMutableDictionary *updated=[self.settings mutableCopy];updated[r[2]]=next;
 if([r[2] isEqual:@"MC3_GRAPHICS_PRESET"])[updated addEntriesFromDictionary:PresetValues(next)];
 NSMutableString *text=[NSMutableString stringWithString:@"# ReDUB iOS settings\n"];
 for(NSString *k in [[updated allKeys] sortedArrayUsingSelector:@selector(compare:)])[text appendFormat:@"%@=%@\n",k,updated[k]];
 NSError *error=nil;if(![text writeToFile:Env() atomically:YES encoding:NSUTF8StringEncoding error:&error]){[self alert:error.localizedDescription];return;}
 self.settings=updated;
 for(NSString *k in updated)setenv([k UTF8String],[updated[k] UTF8String],1);
 [self refreshValueButtons];
 self.status=@"Settings saved for next launch";
}
- (void)alert:(NSString*)message {UIAlertController *a=[UIAlertController alertControllerWithTitle:@"ReDUB" message:message preferredStyle:UIAlertControllerStyleAlert];[a addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:nil]];[self presentViewController:a animated:YES completion:nil];}
- (void)chooseISO {
 UTType *iso=[UTType typeWithFilenameExtension:@"iso"]?:UTTypeDiskImage;
 UIDocumentPickerViewController *picker=[[UIDocumentPickerViewController alloc]initForOpeningContentTypes:@[iso] asCopy:YES];picker.delegate=self;picker.allowsMultipleSelection=NO;[self presentViewController:picker animated:YES completion:nil];
}
- (void)documentPicker:(UIDocumentPickerViewController*)controller didPickDocumentsAtURLs:(NSArray<NSURL*>*)urls {
 [self acceptISOURL:urls.firstObject];
}
- (void)acceptISOURL:(NSURL*)u {
 if(!u||![u.pathExtension.lowercaseString isEqual:@"iso"]){[self alert:@"Choose an Xbox .iso image."];return;}
 self.iso=u;self.page=@"Install";self.status=@"ISO selected. Tap Install game.";[self render];
}
- (void)install {
 if(self.busy||!self.iso)return;
 NSURL *url=self.iso;BOOL scoped=[url startAccessingSecurityScopedResource];self.busy=YES;self.status=@"Reading ISO and preparing installation…";[self render];
 UIApplication.sharedApplication.idleTimerDisabled=YES;
 self.backgroundTask=[UIApplication.sharedApplication beginBackgroundTaskWithName:@"ReDUB disc install" expirationHandler:^{self.status=@"Return to ReDUB to finish installation";if(self.backgroundTask!=UIBackgroundTaskInvalid){[UIApplication.sharedApplication endBackgroundTask:self.backgroundTask];self.backgroundTask=UIBackgroundTaskInvalid;}}];
 dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0),^{@autoreleasepool {
 NSFileManager *fm=NSFileManager.defaultManager;NSString *stage=[Documents() stringByAppendingPathComponent:@"game.installing"],*backup=[Documents() stringByAppendingPathComponent:@"game.previous"];NSError *error=nil;__block BOOL ok=NO;__block NSString *message=nil;
 [fm removeItemAtPath:stage error:nil];
 if(![fm createDirectoryAtPath:stage withIntermediateDirectories:YES attributes:nil error:&error])message=error.localizedDescription;
 else {
  /* Extraction needs room for a second copy while the old install is kept. */
  unsigned long long isoSz=0,have=0;NSDictionary *ia=[fm attributesOfItemAtPath:url.path error:nil];
  isoSz=[ia[NSFileSize] unsignedLongLongValue];
  NSURL *docsURL=[NSURL fileURLWithPath:Documents()];NSNumber *freeNum=nil;
  [docsURL getResourceValue:&freeNum forKey:NSURLVolumeAvailableCapacityForImportantUsageKey error:nil];
  have=[freeNum unsignedLongLongValue];
  unsigned long long need=isoSz+(Exists(Game())?DirSize(Game()):0)+(1ull<<30);
  if(have && have<need)message=[NSString stringWithFormat:@"Not enough free storage: %.1f GB available, about %.1f GB needed. Free space and try again.",have/1e9,need/1e9];
  else {
  ExcludeFromBackup(stage);
  NSFileCoordinator *coordinator=[[NSFileCoordinator alloc]initWithFilePresenter:nil];
  [coordinator coordinateReadingItemAtURL:url options:NSFileCoordinatorReadingWithoutChanges error:&error byAccessor:^(NSURL *local){
  char err[512]={0};ok=ios_disc_extract(local.fileSystemRepresentation,stage.fileSystemRepresentation,Progress,(__bridge void*)self,err,sizeof err);if(!ok)message=[NSString stringWithUTF8String:err];
  }];if(error){ok=NO;message=error.localizedDescription;}
  }
 }
 if(ok){
 /* The old installation remains available until the new one is verified. */
 [fm removeItemAtPath:backup error:nil];BOOL hadOld=Exists(Game());
 if(hadOld && ![fm moveItemAtPath:Game() toPath:backup error:&error])ok=NO;
 if(ok && ![fm moveItemAtPath:stage toPath:Game() error:&error]){ok=NO;if(hadOld)[fm moveItemAtPath:backup toPath:Game() error:nil];}
 if(ok){[fm removeItemAtPath:backup error:nil];ExcludeFromBackup(Game());}else message=error.localizedDescription;
 }
 if(!ok)[fm removeItemAtPath:stage error:nil];if(scoped)[url stopAccessingSecurityScopedResource];CleanupPickedCopy(url);
 dispatch_async(dispatch_get_main_queue(),^{self.busy=NO;UIApplication.sharedApplication.idleTimerDisabled=NO;if(self.backgroundTask!=UIBackgroundTaskInvalid){[UIApplication.sharedApplication endBackgroundTask:self.backgroundTask];self.backgroundTask=UIBackgroundTaskInvalid;}
 if(ok){self.activeGame=Game();self.verified=YES;self.status=@"Installation verified. Ready to play.";self.page=@"Game";[self render];}else{self.status=@"Installation failed — your existing game was preserved";[self render];[self alert:message?:@"Could not read ISO."];}
 });
 }});
}
- (void)verify {
 if(self.busy)return;self.busy=YES;self.status=@"Verifying default.xbe…";
 dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0),^{char err[512];BOOL ok=ios_disc_verify(self.activeGame.fileSystemRepresentation,err,sizeof err);NSString *message=ok?@"Supported Xbox Remix executable verified. Game assets are checked when used by the runtime.":[NSString stringWithUTF8String:err];dispatch_async(dispatch_get_main_queue(),^{self.busy=NO;self.verified=ok;self.status=ok?@"Verification passed":@"Verification failed";[self render];[self alert:message];});});
}
- (void)play {
 if(self.busy)return;if(!self.verified){self.page=@"Install";[self render];return;}
 /* Override stale desktop paths from an imported settings file. Saves must be writable. */
 NSString *save=[Documents() stringByAppendingPathComponent:@"save"];[[NSFileManager defaultManager] createDirectoryAtPath:save withIntermediateDirectories:YES attributes:nil error:nil];setenv("MC3_GAME_DIR",self.activeGame.fileSystemRepresentation,1);setenv("MC3_SAVE_DIR",save.fileSystemRepresentation,1);
 /* Custom ReDUB logos on the boot/menu screens come from the HD texture set.
  * Copy the bundled pack into Documents/hd once so it is writable (texture
  * dumps work there), then point the renderer at it. A user pack already in
  * Documents/hd is left alone. */
 NSString *hdUser=[Documents() stringByAppendingPathComponent:@"hd"],*hdBundle=[NSBundle.mainBundle.bundlePath stringByAppendingPathComponent:@"hd"];
 NSString *ht=[hdUser stringByAppendingPathComponent:@"textures"],*bt=[hdBundle stringByAppendingPathComponent:@"textures"];
 NSFileManager *fm=NSFileManager.defaultManager;
 if(!Exists(ht)&&Exists(bt)){
  [fm createDirectoryAtPath:ht withIntermediateDirectories:YES attributes:nil error:nil];
  for(NSString *f in [fm contentsOfDirectoryAtPath:bt error:nil])[fm copyItemAtPath:[bt stringByAppendingPathComponent:f] toPath:[ht stringByAppendingPathComponent:f] error:nil];
 }
 if(Exists(ht))setenv("RECOMP_GL_HD_DIR",hdUser.fileSystemRepresentation,1);
 else if(Exists(bt))setenv("RECOMP_GL_HD_DIR",hdBundle.fileSystemRepresentation,1);
 {const char *hd=getenv("RECOMP_GL_HD_DIR");fprintf(stderr,"[IOS STARTUP] HD pack dir: %s\n",hd&&*hd?hd:"(default Documents/hd)");fflush(stderr);}
 [self.timer invalidate];self.timer=nil;self.done=YES;UIApplication.sharedApplication.idleTimerDisabled=YES;
}
- (void)files {NSURL *url=[NSURL URLWithString:[@"shareddocuments://" stringByAppendingString:[Documents() stringByAddingPercentEncodingWithAllowedCharacters:NSCharacterSet.URLPathAllowedCharacterSet]]];[UIApplication.sharedApplication openURL:url options:@{} completionHandler:^(BOOL success){if(!success)[self alert:@"Open Files → On My iPhone → ReDUB to access your game, saves, and settings."];}];}
- (void)share {
 NSMutableArray *items=[NSMutableArray array];for(NSString *file in [[NSFileManager defaultManager] contentsOfDirectoryAtPath:Documents() error:nil])if([file hasSuffix:@".txt"]||[file hasSuffix:@".log"])[items addObject:[NSURL fileURLWithPath:[Documents() stringByAppendingPathComponent:file]]];if(!items.count)[items addObject:@"ReDUB iOS: no diagnostics files yet."];
 UIActivityViewController *a=[[UIActivityViewController alloc]initWithActivityItems:items applicationActivities:nil];a.popoverPresentationController.sourceView=self.view;a.popoverPresentationController.sourceRect=CGRectMake(self.view.bounds.size.width/2,self.view.bounds.size.height/2,1,1);[self presentViewController:a animated:YES completion:nil];
}
@end

/* SDL invokes SDL_main on the UIKit main thread. Service UIKit until Play,
 * then restore SDL's original window and continue into the existing runtime. */
int ios_launcher_run(void) {
 @autoreleasepool {
 NSCAssert(NSThread.isMainThread,@"Launcher must run on the UIKit main thread");
 UIWindowScene *scene=nil;
 for(UIScene *candidate in UIApplication.sharedApplication.connectedScenes)if([candidate isKindOfClass:UIWindowScene.class]){
  scene=(UIWindowScene*)candidate;if(candidate.activationState==UISceneActivationStateForegroundActive)break;
 }
 if(!scene){fprintf(stderr,"[IOS STARTUP] No connected scene for launcher\n");return 0;}
 UIWindow *previous=scene.keyWindow;
 UIWindow *window=[[UIWindow alloc]initWithWindowScene:scene];window.windowLevel=UIWindowLevelNormal+1;
 fprintf(stderr,"[IOS STARTUP] presenting launcher\n");
 ReDUBLauncher *vc=[ReDUBLauncher new];window.rootViewController=vc;[window makeKeyAndVisible];
 while(!vc.done){@autoreleasepool{[NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];}}
 /* Do not restore the scene placeholder here. SDL creates its game UIWindow
  * immediately after this function returns. Restoring the placeholder made
  * it remain in front of the OpenGL surface: audio/gameplay continued behind
  * the permanent "Starting ReDUB..." screen. */
 window.hidden=YES;window.rootViewController=nil;
 previous.hidden=YES;
 fprintf(stderr,"[IOS VIDEO] launcher dismissed; scene=%ld placeholder-hidden=%d\n",
         (long)scene.activationState,previous.hidden?1:0);
 return 1;
 }
}
