import json,socket,struct,subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[2];dest=root/'bridge/evidence/network'
info=json.loads(subprocess.check_output(['docker','inspect','reims-av-fixture'],text=True))[0]
assert info['HostConfig']['NetworkMode']=='none' and info['Config']['Image']=='sequoia-reims-metal-profile:local'
assert all('/Sequoia-Reims-VM/storage' not in m['Source'] for m in info['Mounts'])
def bda():
    s=socket.socket(socket.AF_UNIX);s.settimeout(3);s.connect(str(root/'bridge/run/av-fixture/qtest.sock'))
    s.sendall(b'read 0x41a 0x24\n');line=s.makefile('rb').readline().decode();s.close()
    assert line.startswith('OK 0x');return bytes.fromhex(line.split()[1][2:])
before=bda();tail=struct.unpack_from('<H',before,2)[0]
result=subprocess.run(['docker','start','-a','reims-moonlight-keytest'],capture_output=True,text=True,timeout=30)
assert result.returncode in (0,124),result.stderr[-500:]
after=bda();newtail=struct.unpack_from('<H',after,2)[0]
assert newtail==(tail+2 if tail+2<0x3e else 0x1e),(tail,newtail)
assert after[tail-0x1a:tail-0x1a+2]==b'a\x1e',after.hex()
evidence={'guest':'diskless SeaBIOS fixture','before_bda_hex':before.hex(),'after_bda_hex':after.hex(),'tail_before':tail,'tail_after':newtail,'inserted_character':'a','inserted_scan_code':30,'network_to_guest_keyboard_verified':True,'client_exit':result.returncode,'bounded_test_seconds':12}
(dest/'keyboard-verified.json').write_text(json.dumps(evidence,indent=2)+'\n')
print(json.dumps(evidence,indent=2))
