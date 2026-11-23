"""Apply narrowly scoped scene-lifecycle changes to the local SDL 2.30 source.
Backups and a copy of the applied diff are retained. SDL's license is unchanged.
"""
from pathlib import Path
import os,difflib
ROOT=Path(__file__).resolve().parents[1]
SDL=Path(os.environ.get('IOS_SDL_SOURCE',r'C:\Users\Donte\Documents\Codex\ios-toolchain-references\SDL2'))
patches=[]
for filename in ['SDL_uikitappdelegate.m','SDL_uikitwindow.m']:
 p=SDL/'src/video/uikit'/filename;backup=p.with_suffix(p.suffix+'.redub-before-scenes')
 if not backup.exists():backup.write_bytes(p.read_bytes())
 original=backup.read_text();s=original
 if filename=='SDL_uikitappdelegate.m':
  marker='- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)launchOptions\n{'
  assert marker in s
  before,after=s.split(marker,1)
  after=after.replace('#ifdef SDL_IPHONE_LAUNCHSCREEN','#if 0 /* ReDUB: scene delegate owns the launch window. */',1)
  after=after.replace('    NSBundle *bundle = [NSBundle mainBundle];','    extern void ios_diagnostics_start(void);\n    ios_diagnostics_start();\n    NSBundle *bundle = [NSBundle mainBundle];',1)
  old='    [self performSelector:@selector(postFinishLaunch) withObject:nil afterDelay:0.0];'
  assert old in after
  after=after.replace(old,'    /* ReDUB: scene:willConnectToSession: schedules postFinishLaunch. */',1)
  s=before+marker+after
 else:
  old='UIWindow *uiwindow = [[SDL_uikitwindow alloc] initWithFrame:data.uiscreen.bounds];'
  assert old in s
  new='''/* ReDUB alteration: iOS 27 SDK requires scene-associated windows. */
        UIWindowScene *windowScene = nil;
        for (UIScene *scene in UIApplication.sharedApplication.connectedScenes) {
            if ([scene isKindOfClass:[UIWindowScene class]]
                && ((UIWindowScene *)scene).screen == data.uiscreen
                && scene.activationState == UISceneActivationStateForegroundActive) {
                windowScene = (UIWindowScene *)scene;
                break;
            }
        }
        if (!windowScene) return SDL_SetError("No foreground-active UIWindowScene for the SDL display");
        fprintf(stderr, "[IOS VIDEO] creating SDL window; scene=%ld main-thread=%d\\n",
                (long)windowScene.activationState, NSThread.isMainThread ? 1 : 0);
        UIWindow *uiwindow = [[SDL_uikitwindow alloc] initWithWindowScene:windowScene];
        uiwindow.frame = windowScene.coordinateSpace.bounds;'''
  s=s.replace(old,new)
  show='        [data.uiwindow makeKeyAndVisible];'
  assert show in s
  s=s.replace(show,'''        /* Hide the old controllers before presenting SDL. Doing this in
         * the opposite order overlaps appearance transitions on UIKit. */
        /* ReDUB: the scene delegate owns a startup placeholder and the
         * launcher uses a temporary overlay window. Once SDL is visible,
         * neither may remain above or instead of the OpenGL surface. */
        for (UIWindow *other in data.uiwindow.windowScene.windows) {
            if (other != data.uiwindow) other.hidden = YES;
        }
        [data.uiwindow makeKeyAndVisible];
        fprintf(stderr, "[IOS VIDEO] SDL window key=%d visible=%d scene=%ld windows=%lu\\n",
                data.uiwindow.isKeyWindow ? 1 : 0, data.uiwindow.hidden ? 0 : 1,
                (long)data.uiwindow.windowScene.activationState,
                (unsigned long)data.uiwindow.windowScene.windows.count);''',1)
 s='/* ReDUB scene-lifecycle adaptation, 2026-11-16; altered SDL source. */\n'+s
 p.write_text(s)
 patches.extend(difflib.unified_diff(original.splitlines(True),s.splitlines(True),fromfile='a/'+filename,tofile='b/'+filename))
(ROOT/'ios/sdl-scenes.patch').write_text(''.join(patches))
print('Applied SDL scene integration with backups')
