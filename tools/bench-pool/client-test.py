#!/usr/bin/env python3
import importlib.util, json, socket, tempfile, threading, unittest
from pathlib import Path
spec=importlib.util.spec_from_file_location('importer',Path(__file__).with_name('import-state.py'))
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class ImportClientTest(unittest.TestCase):
 def test_handshake_hash_and_stream(self):
  with tempfile.TemporaryDirectory()as tmp:
   p=Path(tmp)/'scene.st0';data=bytearray(m.SIZE);data[:9]=b'STv4 PCSX';m.struct.pack_into('<II',data,32,0x8b410006,1);p.write_bytes(data)
   listener=socket.socket();listener.bind(('127.0.0.1',0));listener.listen();errors=[]
   def server():
    try:
     with listener.accept()[0]as conn:
      reader=conn.makefile('rb');line=reader.readline()
      self.assertTrue(line.startswith(b'test-token state-import Spyro_the_Dragon slot=5 size=4456448 sha256='))
      payload=b'{"ok":true,"ready":true,"size":4456448}';conn.sendall(b'JSON '+str(len(payload)).encode()+b'\n'+payload)
      body=reader.read(m.SIZE);self.assertEqual(bytes(data),body)
      self.assertEqual(m.hashlib.sha256(body).hexdigest(),line.strip().split(b'sha256=')[1].decode())
      payload=b'{"ok":true,"imported":true,"size":4456448}';conn.sendall(b'JSON '+str(len(payload)).encode()+b'\n'+payload)
    except BaseException as error:errors.append(error)
   worker=threading.Thread(target=server);worker.start()
   client=m.Client('127.0.0.1',token='test-token',port=listener.getsockname()[1],timeout=10)
   self.assertTrue(m.import_state(client,'Spyro the Dragon',p,'new',5,rate=10**9)['imported'])
   worker.join();listener.close()
   if errors:raise errors[0]
 def test_reject_malformed_before_connect(self):
  with tempfile.TemporaryDirectory()as tmp:
   p=Path(tmp)/'state';p.write_bytes(bytes(m.SIZE))
   with self.assertRaises(ValueError):m.import_state(m.Client('invalid'), 'game',p,'old',5)
 def test_negative_or_oversized_frames(self):
  import io
  for response in [b'JSON -1\n',b'JSON 4097\n',b'JSON 5\n{}',b'FILE 1\nx',b'JSON 26\n{"ok":false,"error":"deny"}']:
   with self.assertRaises((m.ControlError,ValueError)):m.frame(io.BytesIO(response))
if __name__=='__main__':unittest.main()
