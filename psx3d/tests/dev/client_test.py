#!/usr/bin/env python3
"""Real TCP transport/client integration; only loopback sockets are used."""
import importlib.util
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
from types import SimpleNamespace

HOST_TOOLS = Path(os.environ.get('PSX3D_HOST_TOOLS', '/home/tony/Development/PS1_3ds/devtools'))
sys.path.insert(0, str(HOST_TOOLS))
from psx3dctl import Client, ControlError
import fastbench

class Protocol(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = subprocess.Popen([sys.argv[1]], stdout=subprocess.PIPE, text=True)
        cls.port = int(cls.server.stdout.readline())
        cls.client = Client('127.0.0.1', 'test-token', cls.port, 5)

    @classmethod
    def tearDownClass(cls):
        try:
            cls.client.request('exit')
            cls.server.wait(timeout=5)
            assert cls.server.returncode == 0
        finally:
            if cls.server.poll() is None:
                cls.server.kill()
                cls.server.wait()
            cls.server.stdout.close()

    def test_status_and_token(self):
        self.assertEqual(self.client.request('status')['game'], 'Crash')
        with self.assertRaisesRegex(ControlError, 'unauthorized'):
            Client('127.0.0.1', 'wrong', self.port, 5).request('status')

    def test_history_json_and_missing_counter(self):
        history = self.client.request('stats')['history']
        self.assertEqual(len(history), 60)
        self.assertEqual(history[0]['seq'], 6)
        self.assertEqual(history[-1]['seq'], 65)
        self.assertIsNone(history[-1]['mcycles'])
        self.assertEqual(history[-1]['audio'], 'audio "quoted"\nline')

    def test_main_thread_command_and_shot(self):
        self.assertTrue(self.client.request('load Crash')['applied_on_main'])
        shot = self.client.request('shot both')
        self.assertEqual(shot[:8], b'\x89PNG\r\n\x1a\n')
        self.assertEqual(int.from_bytes(shot[16:20], 'big'), 400)
        self.assertEqual(int.from_bytes(shot[20:24], 'big'), 480)

    def test_update_failure_releases_mailbox(self):
        with self.assertRaisesRegex(ControlError, 'mock update rejected'):
            self.client.request('update mock')
        self.assertTrue(self.client.request('load Again')['ok'])

    def test_file_restrictions_and_reserved_commands(self):
        with self.assertRaisesRegex(ControlError, 'invalid result filename'):
            self.client.request('get ../../config.ini')
        with self.assertRaisesRegex(ControlError, 'reserved command'):
            self.client.request('_updated')
        with self.assertRaises(ControlError):
            self.client.request('set foo=1\nexit')

    def test_recording_archive(self):
        import psx3dctl
        self.assertEqual(self.client.request('recordings Crash')['recordings'], [1])
        self.assertEqual(self.client.request('get recordings/Crash/1.inp'), b'input fixture')
        with patch.object(psx3dctl, 'MAX_RESPONSE', 8*1024*1024):
            data = self.client.request('get recordings/Crash/1.state')
        self.assertEqual(len(data), 4456448)
        self.assertEqual(data, bytes([42])*4456448)
        for name in ('recordings/../1.inp','recordings/Crash/../1.state','recordings/Crash/1.inp.tmp'):
            with self.assertRaisesRegex(ControlError, 'invalid recording filename'):
                self.client.request('get '+name)
        with self.assertRaisesRegex(ControlError, 'invalid recording title'):
            self.client.request('recordings ../config')

    def test_local_server_and_bench_template(self):
        with tempfile.TemporaryDirectory() as d:
            cia = Path(d) / 'test.cia'; cia.write_bytes(b'cia-example')
            with fastbench.serve_cia(cia, '127.0.0.1', 0) as update:
                import urllib.request
                self.assertEqual(urllib.request.urlopen(update.split()[0], timeout=5).read(), b'cia-example')
            bench = Path(d) / 'bench.txt'
            bench.write_text('# comment\ngame=Crash_Bandicoot state={state}\n')
            self.assertEqual(fastbench.bench_text(bench, 'old'), ['game=Crash_Bandicoot state=2'])
            self.assertEqual(fastbench.bench_text(bench, 'new'), ['game=Crash_Bandicoot state=0'])

class Driver(unittest.TestCase):
    def test_parallel_batches_and_artifact_pull(self):
        class FakeClient:
            def __init__(self, console, *args):
                self.console, self.batch_id = console, 'manual'
            def request(self, command):
                if command.startswith('batch-id '):
                    self.batch_id = command.split()[1]
                if command.startswith('bench '):
                    self.runs = command[6:].split(';')
                if command == 'status':
                    return {'session': 'initial', 'game': 'Crash', 'mode': 'library',
                            'bench': {'active': False, 'id': self.batch_id, 'run': 0, 'runs': 1}}
                if command == 'get results.tsv':
                    return ('batch\trun\tstatus\tfps\tspeed_pct\temu_ms\tmcycles\tblack_samples\trequest\n'
                            + f'{self.batch_id}\t1\tok\t59.5\t99\t10\t5\t0\t{self.runs[0]}\n').encode()
                if command == 'get results.md':
                    return b'benchmark report\n'
                return {'ok': True}
        with tempfile.TemporaryDirectory() as d:
            directory = Path(d)
            bench = directory / 'bench.txt'
            bench.write_text('game=Crash_Bandicoot state={state} seconds=1 shot=0\n')
            args = SimpleNamespace(token='', port=5555, timeout=5, batch_id='test', bench=bench,
                                   out=directory / 'out')
            with patch.object(fastbench, 'Client', FakeClient):
                from concurrent.futures import ThreadPoolExecutor
                with ThreadPoolExecutor(max_workers=2) as pool:
                    jobs = [pool.submit(fastbench.run_console, console, args, None) for console in ('old', 'new')]
                    results = dict(job.result() for job in jobs)
            self.assertIn('state=2', results['old'][0]['request'])
            self.assertIn('state=0', results['new'][0]['request'])
            self.assertEqual(results['old'][0]['batch'], 'test-old')
            self.assertEqual(results['new'][0]['batch'], 'test-new')
            self.assertTrue((args.out / 'old/results.tsv').exists())
            self.assertTrue((args.out / 'new/results.md').exists())

if __name__ == '__main__':
    unittest.main(argv=[sys.argv[0]])
