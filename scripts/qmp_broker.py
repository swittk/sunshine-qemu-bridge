#!/usr/bin/env python3
"""Private QEMU QMP -> GIO peer FD broker for Sunshine's repeated encoder probes.

For every local capture request, acquire a fresh QEMU Display1 peer through
QMP getfd/add_client, then pass that peer FD over SCM_RIGHTS. QEMU retains
its existing VNC backend; no host TCP ports or system D-Bus are needed.
"""
import argparse
import array
import os
from pathlib import Path
import signal
import socket
import stat
import subprocess
import sys
import threading

from qmp_attach import attach

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--qmp",required=True)
    parser.add_argument("--socket",required=True)
    parser.add_argument("--exec",nargs=argparse.REMAINDER,required=True)
    args=parser.parse_args()
    cmd=args.exec
    if cmd and cmd[0]=="--":cmd=cmd[1:]
    if not cmd:raise RuntimeError("missing Sunshine command")
    path=Path(args.socket)
    if not path.is_absolute():raise RuntimeError("broker socket must be absolute")
    path.parent.mkdir(parents=True,exist_ok=True)
    if path.exists():
        st=path.lstat()
        if not stat.S_ISSOCK(st.st_mode) or st.st_uid!=os.getuid():
            raise RuntimeError("refusing to replace unrelated broker path")
        path.unlink()
    listener=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM)
    listener.bind(str(path))
    os.chmod(path,0o600)
    listener.listen(8)
    listener.settimeout(.5)
    halted=threading.Event()
    requests=[0]
    failures=[0]
    def serve():
        while not halted.is_set():
            try:
                conn,_=listener.accept()
            except socket.timeout:
                continue
            except OSError:
                break
            with conn:
                try:
                    with attach(args.qmp,authenticate=False) as peer:
                        fds=array.array("i",[peer.fileno()])
                        n=conn.sendmsg([b"F"],[(socket.SOL_SOCKET,socket.SCM_RIGHTS,fds)])
                        if n!=1:raise RuntimeError("broker FD transfer incomplete")
                        requests[0]+=1
                except Exception as error:
                    failures[0]+=1
                    print("QMP_BROKER_REQUEST_ERROR",type(error).__name__,str(error)[:180],flush=True)
                    try:conn.sendall(b"E")
                    except OSError:pass
    thread=threading.Thread(target=serve,daemon=True)
    thread.start()
    env=os.environ.copy()
    env["QEMU_DBUS_ADDRESS"]="broker:"+str(path)
    env["QEMU_DBUS_DESTINATION"]=""
    print("QMP_BROKER_READY socket="+str(path),flush=True)
    try:
        child=subprocess.Popen(cmd,env=env)
        def signal_child(signum,_):
            if child.poll() is None:child.send_signal(signum)
        signal.signal(signal.SIGTERM,signal_child)
        signal.signal(signal.SIGINT,signal_child)
        code=child.wait()
        print("QMP_BROKER_RESULT requests="+str(requests[0])+" errors="+str(failures[0])+
              " child_exit="+str(code),flush=True)
        return code
    finally:
        halted.set()
        listener.close()
        thread.join(timeout=3)
        if path.exists() and stat.S_ISSOCK(path.lstat().st_mode):
            path.unlink()
if __name__=="__main__":
    try:sys.exit(main())
    except Exception as ex:
        print("QMP_BROKER_FATAL",type(ex).__name__,str(ex)[:200],file=sys.stderr)
        sys.exit(1)
