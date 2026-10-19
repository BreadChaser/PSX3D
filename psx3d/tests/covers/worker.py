#!/usr/bin/env python3
import http.server,os,subprocess,sys,tempfile,threading,time
from pathlib import Path
root=Path(__file__).resolve().parents[3]
class Handler(http.server.BaseHTTPRequestHandler):
 def do_GET(self):
  if self.path=='/missing.png':self.send_error(404);return
  if self.path=='/error.png':self.send_error(503);return
  self.send_response(200);self.send_header('Content-Length','99999' if self.path=='/slow.png' else str(len(png)));self.end_headers()
  if self.path=='/slow.png':time.sleep(.5);return
  self.wfile.write(png)
 def log_message(self,*a):pass
# Small RGBA PNG generated with libpng-independent standard library.
import struct,zlib
def chunk(t,b):return struct.pack('>I',len(b))+t+b+struct.pack('>I',zlib.crc32(t+b))
png=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',1,1,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(b'\0\xff\x80\0\xff'))+chunk(b'IEND',b'')
with http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler) as server,tempfile.TemporaryDirectory() as tmp:
 thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
 libs=subprocess.check_output(['pkg-config','--cflags','--libs','libpng','libcurl'],text=True).split()
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Wno-deprecated-declarations','-g','-fsanitize=address,undefined',f'-DTHUMB_URL="http://127.0.0.1:{server.server_port}/"',str(root/'psx3d/tests/covers/worker.c'),str(root/'psx3d/source/covers_png.c'),'-Wl,--wrap=curl_multi_poll',*libs,'-o',tmp+'/check'],check=True)
 subprocess.run([tmp+'/check'],cwd=tmp,check=True)
 server.shutdown();thread.join()
