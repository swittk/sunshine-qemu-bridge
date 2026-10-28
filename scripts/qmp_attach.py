#!/usr/bin/env python3
"""Give an authorized local client a new QEMU Display1 peer.
Only QMP getfd/add_client are used. GIO owns D-Bus authentication; no manual
SASL exchange or authentication override is performed here.
"""
import array
import json
import socket
import uuid


def receive(sock):
    line=bytearray()
    while not line.endswith(b'\n'):
        byte=sock.recv(1)
        if not byte:
            raise RuntimeError('QMP closed before its response')
        line.extend(byte)
        if len(line)>65536:
            raise RuntimeError('QMP response too large')
    return json.loads(line)


def qmp_reply(sock, command, passed_fd=None):
    command=dict(command,id=uuid.uuid4().hex)
    payload=json.dumps(command,separators=(',',':')).encode()+b'\r\n'
    if passed_fd is not None:
        count=sock.sendmsg([payload],[(socket.SOL_SOCKET,socket.SCM_RIGHTS,array.array('i',[passed_fd]))])
        if count<len(payload):sock.sendall(payload[count:])
    else:
        sock.sendall(payload)
    while True:
        reply=receive(sock)
        if reply.get('id')!=command['id']:
            continue
        if 'error' in reply:
            raise RuntimeError('QMP '+command['execute']+': '+str(reply['error']))
        return reply.get('return')


def attach(qmp_socket, authenticate=False):
    if authenticate:
        raise ValueError('The receiving GIO client must perform D-Bus authentication')
    client=server=None
    with socket.socket(socket.AF_UNIX,socket.SOCK_STREAM) as monitor:
        monitor.settimeout(5)
        monitor.connect(str(qmp_socket))
        if 'QMP' not in receive(monitor):raise RuntimeError('Expected QMP greeting')
        qmp_reply(monitor,{'execute':'qmp_capabilities'})
        client,server=socket.socketpair(socket.AF_UNIX,socket.SOCK_STREAM)
        fdname='reims-'+uuid.uuid4().hex
        try:
            qmp_reply(monitor,{'execute':'getfd','arguments':{'fdname':fdname}},server.fileno())
            qmp_reply(monitor,{'execute':'add_client','arguments':{
                'protocol':'@dbus-display','fdname':fdname}})
        except Exception:
            client.close()
            try:qmp_reply(monitor,{'execute':'closefd','arguments':{'fdname':fdname}})
            except Exception:pass
            raise
        finally:
            server.close()
    return client
