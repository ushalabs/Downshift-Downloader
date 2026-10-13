"""Temporary single-host HKCU registration for isolated tests; preserve any earlier value."""
import json, pathlib, sys, winreg
NAME="org.ushalabs.downshift.test" if "--isolated" in sys.argv else "org.ushalabs.downshift"
sys.argv=[arg for arg in sys.argv if arg!="--isolated"]
KEY="Software\\Google\\Chrome\\NativeMessagingHosts\\"+NAME
VIEW=winreg.KEY_WOW64_64KEY
if sys.argv[1]=="install":
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER,KEY,0,winreg.KEY_READ|VIEW) as key: previous=winreg.QueryValueEx(key,"")[0]
    except FileNotFoundError: previous=None
    manifest=pathlib.Path(sys.argv[3]).resolve();backup=pathlib.Path(sys.argv[4]);backup.write_text(json.dumps({"previous":previous}),encoding="utf-8")
    manifest.write_text(json.dumps(dict(name=NAME,description="Downshift isolated integration test",path=str(pathlib.Path(sys.argv[2]).resolve()),type="stdio",allowed_origins=["chrome-extension://empnpdkfnffpcgnfgdndkbielplcoioe/"])),encoding="utf-8")
    with winreg.CreateKeyEx(winreg.HKEY_CURRENT_USER,KEY,0,winreg.KEY_WRITE|VIEW) as key:winreg.SetValueEx(key,"",0,winreg.REG_SZ,str(manifest))
elif sys.argv[1]=="restore":
    backup=pathlib.Path(sys.argv[2])
    if backup.exists():
        previous=json.loads(backup.read_text(encoding="utf-8"))["previous"]
        if previous is None:
            try:winreg.DeleteKeyEx(winreg.HKEY_CURRENT_USER,KEY,VIEW)
            except FileNotFoundError:pass
        else:
            with winreg.CreateKeyEx(winreg.HKEY_CURRENT_USER,KEY,0,winreg.KEY_WRITE|VIEW) as key:winreg.SetValueEx(key,"",0,winreg.REG_SZ,previous)
