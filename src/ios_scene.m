#import <UIKit/UIKit.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include "SDL.h"

#ifndef MC3_VERSION
#  define MC3_VERSION "dev"
#endif
#ifndef MC3_BUILD_ID
#  define MC3_BUILD_ID "ios-dev"
#endif

static atomic_int s_ios_scene_active;
int ios_scene_is_active(void) { return atomic_load_explicit(&s_ios_scene_active,memory_order_acquire); }

static void Uncaught(NSException *exception) {
 fprintf(stderr,"[IOS EXCEPTION] %s: %s\n%s\n",exception.name.UTF8String,exception.reason.UTF8String,[[exception callStackSymbols] componentsJoinedByString:@"\n"].UTF8String);
 fflush(stderr);
}
void ios_diagnostics_start(void) {
 NSString *doc=NSSearchPathForDirectoriesInDomains(NSDocumentDirectory,NSUserDomainMask,YES).firstObject;
 [NSFileManager.defaultManager createDirectoryAtPath:doc withIntermediateDirectories:YES attributes:nil error:nil];
 NSString *log=[doc stringByAppendingPathComponent:@"mcdub3remix-startup.log"];
 /* Keep one previous log; the file is appended for the life of the install. */
 {NSFileManager *fm=NSFileManager.defaultManager;NSDictionary *a=[fm attributesOfItemAtPath:log error:nil];
  if([a[NSFileSize] unsignedLongLongValue] > 4*1024*1024){NSString *old=[log stringByAppendingString:@".1"];
   [fm removeItemAtPath:old error:nil];[fm moveItemAtPath:log toPath:old error:nil];}}
 freopen(log.fileSystemRepresentation,"a",stderr);setvbuf(stderr,NULL,_IONBF,0);
 NSSetUncaughtExceptionHandler(Uncaught);
 fprintf(stderr,"[IOS STARTUP] ReDUB %s, %s, UIKit screen framebuffer; OS %s\n",MC3_VERSION,MC3_BUILD_ID,UIDevice.currentDevice.systemVersion.UTF8String);
}
@interface ReDUBLaunchPlaceholder : UIViewController
@end
@implementation ReDUBLaunchPlaceholder
- (UIInterfaceOrientationMask)supportedInterfaceOrientations{return UIInterfaceOrientationMaskLandscape;}
- (BOOL)prefersStatusBarHidden{return YES;}
- (void)viewDidLoad {
 [super viewDidLoad];self.view.backgroundColor=[UIColor colorWithRed:8/255. green:13/255. blue:26/255. alpha:1];
 UILabel *label=[UILabel new];label.text=@"Starting ReDUB...";label.textColor=UIColor.whiteColor;label.textAlignment=NSTextAlignmentCenter;label.translatesAutoresizingMaskIntoConstraints=NO;[self.view addSubview:label];
 [NSLayoutConstraint activateConstraints:@[[label.centerXAnchor constraintEqualToAnchor:self.view.centerXAnchor],[label.centerYAnchor constraintEqualToAnchor:self.view.centerYAnchor]]];
}
@end
@interface ReDUBSceneDelegate : UIResponder<UIWindowSceneDelegate>
@property(nonatomic,strong) UIWindow *window;
@end
@implementation ReDUBSceneDelegate
- (void)scene:(UIScene*)scene willConnectToSession:(UISceneSession*)session options:(UISceneConnectionOptions*)options {
 if(![scene isKindOfClass:UIWindowScene.class])return;
 fprintf(stderr,"[IOS STARTUP] scene connected\n");
 self.window=[[UIWindow alloc]initWithWindowScene:(UIWindowScene*)scene];
 self.window.rootViewController=[ReDUBLaunchPlaceholder new];[self.window makeKeyAndVisible];
 extern void ios_launcher_receive_url(NSURL *url);
 if(options.URLContexts.anyObject.URL)ios_launcher_receive_url(options.URLContexts.anyObject.URL);
 /* Return to UIKit before starting SDL_main. A second scene must never boot
  * the native game a second time. Multiple-scene support is disabled in plist. */
 static BOOL started=NO;if(started)return;started=YES;
 id delegate=UIApplication.sharedApplication.delegate;
 SEL start=NSSelectorFromString(@"postFinishLaunch");
 if([delegate respondsToSelector:start]){
  [delegate performSelector:start withObject:nil afterDelay:0.0];
 }else fprintf(stderr,"[IOS STARTUP] SDL postFinishLaunch is missing\n");
}
- (void)scene:(UIScene*)scene openURLContexts:(NSSet<UIOpenURLContext*>*)contexts {
 extern void ios_launcher_receive_url(NSURL *url);
 NSURL *url=contexts.anyObject.URL;if(url)ios_launcher_receive_url(url);
}
- (void)sceneDidBecomeActive:(UIScene*)scene {
 atomic_store_explicit(&s_ios_scene_active,1,memory_order_release);
 fprintf(stderr,"[IOS LIFECYCLE] foreground active; video=%u\n",SDL_WasInit(SDL_INIT_VIDEO)?1:0);
 extern void SDL_OnApplicationDidBecomeActive(void);
 if(SDL_WasInit(SDL_INIT_VIDEO))SDL_OnApplicationDidBecomeActive();
 extern void nv2a_gl_note_resume(void);
 if(SDL_WasInit(SDL_INIT_VIDEO))nv2a_gl_note_resume();
}
- (void)sceneWillResignActive:(UIScene*)scene {
 atomic_store_explicit(&s_ios_scene_active,0,memory_order_release);
 fprintf(stderr,"[IOS LIFECYCLE] resigning active; video=%u\n",SDL_WasInit(SDL_INIT_VIDEO)?1:0);
 extern void SDL_OnApplicationWillResignActive(void);
 extern void xbox_touchpad_suspend(void);
 if(SDL_WasInit(SDL_INIT_VIDEO)){SDL_OnApplicationWillResignActive();xbox_touchpad_suspend();}
}
- (void)sceneDidEnterBackground:(UIScene*)scene {
 atomic_store_explicit(&s_ios_scene_active,0,memory_order_release);
 fprintf(stderr,"[IOS LIFECYCLE] entered background; video=%u\n",SDL_WasInit(SDL_INIT_VIDEO)?1:0);
 extern void SDL_OnApplicationDidEnterBackground(void);
 extern void xbox_touchpad_suspend(void);
 if(SDL_WasInit(SDL_INIT_VIDEO)){SDL_OnApplicationDidEnterBackground();xbox_touchpad_suspend();}
}
- (void)sceneWillEnterForeground:(UIScene*)scene {
 fprintf(stderr,"[IOS LIFECYCLE] entering foreground; video=%u\n",SDL_WasInit(SDL_INIT_VIDEO)?1:0);
 extern void SDL_OnApplicationWillEnterForeground(void);
 if(SDL_WasInit(SDL_INIT_VIDEO))SDL_OnApplicationWillEnterForeground();
}
@end
