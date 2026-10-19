import importlib.util,unittest,tempfile,socket,sys,contextlib,io
from pathlib import Path
from unittest.mock import patch
s=importlib.util.spec_from_file_location('driver',Path(__file__).with_name('hardware.py'));d=importlib.util.module_from_spec(s);s.loader.exec_module(d)
class Checks(unittest.TestCase):
 def test_dry_no_network(self):
  with patch.object(sys,'argv',['hardware.py','--dry-run']),patch.object(socket,'socket',side_effect=AssertionError('socket')),contextlib.redirect_stdout(io.StringIO()):d.main()
 def test_marker(self):
  with tempfile.TemporaryDirectory() as t,patch.object(d,'MARKER',Path(t)/'OLD_UNAVAILABLE'):
   d.clearance();d.MARKER.write_text('uncertain')
   with self.assertRaises(RuntimeError):d.clearance()
 def test_settings(self):
  line='state=2 record=off hack_spunearest=on frameskip=off'
  status=dict(mode='playing',renderer='gpu',state=2,settings=dict(record=False,hack_spunearest=True,pmu0=0,pmu1=2,clock='57',frameskip='disabled'))
  d.check_settings(status,line)
  status['settings']['hack_spunearest']=False
  with self.assertRaises(AssertionError):d.check_settings(status,line)
if __name__=='__main__':unittest.main()
