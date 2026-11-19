Open PegglePort.app. The first time, right-click it and choose Open, because the app is not signed with a paid developer certificate.
If macOS says the app is damaged, run this once in Terminal from the folder that holds the app:

  xattr -dr com.apple.quarantine PegglePort.app

PegglePort looks for your copy of Peggle Deluxe, Peggle Nights, or Peggle Extreme. If none is found, choose the folder containing main.pak.
