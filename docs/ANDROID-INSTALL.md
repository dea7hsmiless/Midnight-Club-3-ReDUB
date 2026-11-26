# Installing ReDUB on Android

Two APKs ship, and the second exists because Google blocks the first for a reason.

## Why Android says "App blocked to protect your device"

Google Play Protect has a feature it calls **enhanced fraud protection**. When you open an APK
from a *browser, a messaging app or a file manager* — an "internet-sideloading source" — Play
Protect reads the permissions the app declares. If it finds any of these four, it blocks the
install automatically, with no way past the dialog:

- `RECEIVE_SMS`
- `READ_SMS`
- a notification listener (`BIND_NOTIFICATION_LISTENER_SERVICE`)
- an accessibility service (`BIND_ACCESSIBILITY_SERVICE`)

ReDUB declares **a notification listener**, for one feature: the in-game radio reads the song
playing on the phone through `MediaSessionManager`, which Android only returns to an app holding
that permission. It is not a malware finding, and nothing about the game is unsafe.

It is **regional**. Google runs the protection in select markets and does not publish the full
list. Publicly announced rollouts: Singapore, Thailand, Brazil, India, Kenya, Nigeria,
Philippines, South Africa, Vietnam, Hong Kong, Malaysia. Elsewhere the same APK installs normally.

It is also **not new in 0.2.6**. The 0.2.5 APK declares exactly the same service.

## The two APKs

| File | Declares | Loses |
|---|---|---|
| `ReDUB-<version>-android.apk` | notification listener, `MANAGE_EXTERNAL_STORAGE` | — |
| `ReDUB-<version>-android-lite.apk` | neither | now-playing radio, and a disc outside the app's reachable folders |

Same package, same version code, same signing key: they replace each other. Install one.

The second one installs everywhere the first is blocked, because the check only looks at what
the app declares.

## If you want the full build

`adb install` is not an internet-sideloading source, so the check does not run and Play Protect
stays enabled:

```
adb install -r ReDUB-<version>-android.apk
```

On Samsung, turn off **Settings → Security and privacy → Auto Blocker** first: it blocks
sideloading *and* USB installs independently.

## Building both

```
cd android
./gradlew assembleRelease                        # the full APK
./gradlew -PMC3_MUSIC=0 assembleRelease          # the no-music APK
```

The two builds clear the APK output folder, so copy each one out before starting the other.

`-PMC3_MUSIC=0` swaps the manifest rather than merging an overlay:
`app/src/lite/AndroidManifest.xml` replaces `app/src/main/AndroidManifest.xml`. The merger
cannot remove a service's `android:permission` attribute in a way that keeps
`BIND_NOTIFICATION_LISTENER_SERVICE` out of the packaged manifest.

Two hand-maintained manifests drift, so `tools/check_android_manifests.py` compares them and
fails unless the only differences are the two entries above:

```
python tools/check_android_manifests.py
```

It checks the property directly as well — no banned permission, and no service holding the
notification-listener or accessibility binding — so the check still fails if someone keeps a
sensitive declaration under a different service name.
