import importlib.util
import json
import os
from pathlib import Path
import queue
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from unittest import mock


SERVER = Path(__file__).resolve().parents[1] / 'examples/mods/mcp_agent/server.py'
VERSIONS = ('2025-11-25', '2025-06-18', '2025-03-26', '2024-11-05')


class Client:
    def __init__(self, data_dir):
        self.process = subprocess.Popen(
            [sys.executable, '-u', str(SERVER), '--data-dir', str(data_dir)],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        )
        self.messages = queue.Queue()
        assert self.process.stdin is not None
        assert self.process.stdout is not None
        assert self.process.stderr is not None
        self.input = self.process.stdin
        self.output = self.process.stdout
        self.errors = self.process.stderr
        self.error = None
        self.reader = threading.Thread(target=self.read_output, daemon=True)
        self.reader.start()
        self.next_id = 0

    def read_output(self):
        for line in self.output:
            self.messages.put(line)
        self.messages.put(b'')

    def send(self, message):
        self.send_raw(json.dumps(message).encode('utf-8') + b'\n')

    def send_raw(self, data):
        self.input.write(data)
        self.input.flush()

    def receive(self, timeout=5):
        line = self.messages.get(timeout=timeout)
        if not line:
            raise AssertionError('Companion closed stdout before responding')
        return json.loads(line)

    def request(self, method, params=None):
        self.next_id += 1
        message = {'jsonrpc': '2.0', 'id': self.next_id, 'method': method}
        if params is not None:
            message['params'] = params
        self.send(message)
        return self.receive()

    def initialize(self, version='2025-11-25'):
        response = self.request('initialize', {
            'protocolVersion': version, 'capabilities': {},
            'clientInfo': {'name': 'unittest', 'version': '1'},
        })
        self.send({'jsonrpc': '2.0', 'method': 'notifications/initialized'})
        return response

    def close(self):
        if self.error is not None:
            return self.error
        if not self.input.closed:
            try:
                self.input.close()
            except OSError:
                pass
        try:
            self.process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait(timeout=5)
        self.reader.join(timeout=5)
        self.error = self.errors.read().decode('utf-8', errors='replace')
        self.output.close()
        self.errors.close()
        return self.error


class MCPServerTest(unittest.TestCase):
    def setUp(self):
        scratch = Path(os.environ.get('TMPDIR', Path.home() / '.cache/peggleport-tests'))
        scratch.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=scratch)
        self.addCleanup(self.temporary.cleanup)
        self.data_dir = Path(self.temporary.name)

    def client(self):
        client = Client(self.data_dir)
        self.addCleanup(client.close)
        return client

    def write_state(self, **changes):
        state = {
            'session': 'board-session', 'armed': True, 'available': True,
            'reason': '', 'width': 650, 'height': 550, 'tick': 100,
            'phase': 'aim', 'score': 1200, 'balls_left': 10, 'orange_left': 25,
            'aim_x': 320, 'aim_y': 400,
            'pegs': [{'id': 1, 'x': 300, 'y': 400, 'type': 'orange'}], 'balls': [],
        }
        state.update(changes)
        temporary = self.data_dir / 'state.tmp'
        temporary.write_text(json.dumps(state), encoding='utf-8')
        os.replace(temporary, self.data_dir / 'state.json')
        return state

    def call(self, client, name, arguments=None):
        return client.request('tools/call', {'name': name, 'arguments': {} if arguments is None else arguments})

    def await_request(self):
        deadline = time.monotonic() + 3
        last_error = None
        while time.monotonic() < deadline:
            try:
                return json.loads((self.data_dir / 'request.json').read_text(encoding='utf-8'))
            except OSError as error:
                last_error = error
                time.sleep(0.005)
        self.fail('Companion did not publish a game request; last read error: ' + str(last_error))

    def respond(self, response):
        temporary = self.data_dir / 'response.tmp'
        temporary.write_text(json.dumps(response), encoding='utf-8')
        os.replace(temporary, self.data_dir / 'response.json')

    def assert_rejected_without_command(self, client, name, arguments):
        started = time.monotonic()
        reply = self.call(client, name, arguments)
        self.assertLess(time.monotonic() - started, 0.5, 'Invalid action reached the game exchange instead of being rejected')
        self.assertIs(reply['result']['isError'], True)
        self.assertTrue(json.loads(reply['result']['content'][0]['text'])['error'])
        self.assertFalse((self.data_dir / 'request.json').exists())
        self.assertFalse((self.data_dir / 'request.tmp').exists())

    def test_initialize_negotiates_legacy_versions_and_ping(self):
        for version in (*VERSIONS, '2099-01-01'):
            with self.subTest(version=version):
                client = self.client()
                response = client.initialize(version)
                self.assertEqual(response['jsonrpc'], '2.0')
                self.assertEqual(response['id'], 1)
                self.assertEqual(response['result']['protocolVersion'], version if version in VERSIONS else VERSIONS[0])
                self.assertEqual(response['result']['capabilities'], {'tools': {'listChanged': False}})
                self.assertEqual(client.request('ping')['result'], {})
                client.close()
                self.assertEqual(client.process.returncode, 0)

    def test_protocol_rejects_bad_messages_without_losing_stream(self):
        client = self.client()
        client.initialize()
        cases = (
            (b'{\n', -32700),
            (b'\xff\n', -32700),
            (b'{"jsonrpc":"2.0","id":9,"method":"ping","params":{"n":NaN}}\n', -32700),
            (b'[]\n', -32600),
            (b'null\n', -32600),
            (b'{"jsonrpc":"1.0","id":9,"method":"ping"}\n', -32600),
            (b'{"jsonrpc":"2.0","id":true,"method":"ping"}\n', -32600),
            (b'{"jsonrpc":"2.0","id":null,"method":"ping"}\n', -32600),
            (b'{"jsonrpc":"2.0","id":9,"method":17}\n', -32600),
            (b'{"jsonrpc":"2.0","id":9,"method":"missing"}\n', -32601),
            (b'{"jsonrpc":"2.0","id":9,"method":"ping","params":[]}\n', -32602),
            (b'x' * (1024 * 1024 + 1) + b'\n', -32700),
        )
        for raw, code in cases:
            with self.subTest(raw=raw[:90]):
                client.send_raw(raw)
                self.assertEqual(client.receive()['error']['code'], code)
                self.assertEqual(client.request('ping')['result'], {})

    def test_notifications_produce_no_response(self):
        self.write_state()
        client = self.client()
        client.initialize()
        for method in ('ping', 'missing', 'tools/call', 'notifications/cancelled'):
            client.send({'jsonrpc': '2.0', 'method': method, 'params': {}})
        client.send({'jsonrpc': '2.0', 'method': 'tools/call', 'params': {'name': 'peggle_fire', 'arguments': {}}})
        reply = client.request('ping')
        self.assertEqual(reply['id'], client.next_id)
        self.assertEqual(reply['result'], {})
        self.assertFalse((self.data_dir / 'request.json').exists())

    def test_initialization_requires_valid_parameters_and_ready_notification(self):
        client = self.client()
        self.assertEqual(client.request('tools/list')['error']['code'], -32000)
        for params in ({}, {'protocolVersion': 1}, {'protocolVersion': '2025-11-25', 'capabilities': [], 'clientInfo': {}}):
            self.assertEqual(client.request('initialize', params)['error']['code'], -32602)
        client.request('initialize', {
            'protocolVersion': VERSIONS[0], 'capabilities': {},
            'clientInfo': {'name': 'unittest', 'version': '1'},
        })
        self.assertEqual(client.request('tools/list')['error']['code'], -32000)
        client.send({'jsonrpc': '2.0', 'method': 'notifications/initialized'})
        self.assertEqual(client.request('initialize', {})['error']['code'], -32600)

    def test_one_companion_holds_data_directory_lock_until_exit(self):
        first = self.client()
        first.initialize()
        lock = self.data_dir / 'bridge.lock'
        self.assertTrue(lock.is_file())
        second = self.client()
        second.process.wait(timeout=5)
        self.assertNotEqual(second.process.returncode, 0)
        self.assertIn('bridge.lock', second.close())
        self.assertTrue(lock.is_file())
        self.assertEqual(first.close(), '')
        self.assertFalse(lock.exists())
        third = self.client()
        third.initialize()
        self.assertTrue(lock.is_file())

    def test_foreign_lock_is_never_removed(self):
        lock = self.data_dir / 'bridge.lock'
        lock.write_text('another companion', encoding='utf-8')
        client = self.client()
        client.process.wait(timeout=5)
        self.assertIn('closing', client.close())
        self.assertEqual(lock.read_text(encoding='utf-8'), 'another companion')
        lock.unlink()
        client = self.client()
        client.initialize()
        lock.write_text('replacement lock', encoding='utf-8')
        client.close()
        self.assertEqual(lock.read_text(encoding='utf-8'), 'replacement lock')

    def test_data_directory_must_be_explicit_and_absolute(self):
        for arguments in ([], ['--data-dir', 'userdata/mod-data/mcp_agent']):
            result = subprocess.run([sys.executable, str(SERVER), *arguments], capture_output=True, timeout=5)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(result.stdout, b'')
            self.assertIn(b'--data-dir', result.stderr)

    def test_tool_discovery_and_observation_follow_negotiated_version(self):
        state = self.write_state(armed=False, available=False, reason='not armed')
        for version in VERSIONS:
            with self.subTest(version=version):
                client = self.client()
                client.initialize(version)
                tools = {tool['name']: tool for tool in client.request('tools/list')['result']['tools']}
                self.assertEqual(set(tools), {'peggle_observe', 'peggle_aim', 'peggle_fire', 'peggle_flippers', 'peggle_wait'})
                for tool in tools.values():
                    self.assertEqual(tool['inputSchema']['type'], 'object')
                    self.assertIs(tool['inputSchema']['additionalProperties'], False)
                self.assertEqual(tools['peggle_aim']['inputSchema']['required'], ['x', 'y'])
                self.assertEqual(tools['peggle_flippers']['inputSchema']['properties']['pressed']['type'], 'boolean')
                self.assertEqual(tools['peggle_wait']['inputSchema']['properties']['seconds']['maximum'], 10)
                result = self.call(client, 'peggle_observe')['result']
                self.assertIs(result['isError'], False)
                self.assertEqual(result['content'][0]['type'], 'text')
                self.assertEqual(json.loads(result['content'][0]['text']), state)
                if version in VERSIONS[:2]:
                    self.assertEqual(result['structuredContent'], state)
                else:
                    self.assertNotIn('structuredContent', result)
                client.close()

    def test_observation_rejects_missing_stale_malformed_or_oversized_state(self):
        client = self.client()
        client.initialize()
        state_path = self.data_dir / 'state.json'
        cases = ('missing', 'stale', 'malformed', 'array', 'oversized', 'future', 'nonfinite')
        for case in cases:
            with self.subTest(case=case):
                if case != 'missing':
                    self.write_state()
                if case == 'stale':
                    os.utime(state_path, (time.time() - 3, time.time() - 3))
                if case == 'future':
                    os.utime(state_path, (time.time() + 60, time.time() + 60))
                if case in ('malformed', 'array', 'oversized', 'nonfinite'):
                    state_path.write_text({'malformed': '{', 'array': '[]', 'oversized': ' ' * (1024 * 1024 + 1), 'nonfinite': '{"tick":1e999}'}[case], encoding='utf-8')
                result = self.call(client, 'peggle_observe')['result']
                self.assertIs(result['isError'], True)
                self.assertIs(json.loads(result['content'][0]['text'])['ok'], False)
                self.assertTrue(json.loads(result['content'][0]['text'])['error'])
                self.assertEqual(client.request('ping')['result'], {})

    def test_wait_reads_a_new_snapshot_without_writing_a_command(self):
        self.write_state(armed=False)
        client = self.client()
        client.initialize()
        client.send({'jsonrpc': '2.0', 'id': 'wait', 'method': 'tools/call', 'params': {'name': 'peggle_wait', 'arguments': {'seconds': 0.15}}})
        time.sleep(0.05)
        state = self.write_state(armed=False, tick=101)
        result = client.receive()['result']
        self.assertIs(result['isError'], False)
        self.assertEqual(json.loads(result['content'][0]['text']), state)
        self.assertFalse((self.data_dir / 'request.json').exists())
        self.assertIs(self.call(client, 'peggle_wait', {'seconds': 0})['result']['isError'], False)

    def test_read_only_tools_reject_invalid_arguments(self):
        self.write_state()
        client = self.client()
        client.initialize()
        cases = [
            ('peggle_observe', {'unexpected': True}),
            ('peggle_wait', {}),
            ('peggle_wait', {'seconds': -0.1}),
            ('peggle_wait', {'seconds': 10.1}),
            ('peggle_wait', {'seconds': True}),
            ('peggle_wait', {'seconds': '1'}),
            ('peggle_wait', {'seconds': None}),
            ('peggle_wait', {'seconds': 0, 'extra': 1}),
        ]
        for name, arguments in cases:
            with self.subTest(name=name, arguments=arguments):
                result = self.call(client, name, arguments)['result']
                self.assertIs(result['isError'], True)
                self.assertTrue(json.loads(result['content'][0]['text'])['error'])
        self.assertEqual(self.call(client, 'unknown')['error']['code'], -32602)
        self.assertEqual(self.call(client, 'peggle_observe', [])['error']['code'], -32602)


    def test_aim_exchanges_an_atomic_request_for_the_actual_game_result(self):
        self.write_state()
        self.respond({'id': 'old-request', 'ok': True, 'state': {'score': -1}})
        client = self.client()
        client.initialize()
        started = time.time()
        client.send({'jsonrpc': '2.0', 'id': 'aim', 'method': 'tools/call', 'params': {'name': 'peggle_aim', 'arguments': {'x': 320, 'y': 400}}})
        request = self.await_request()
        self.assertEqual(set(request), {'id', 'session', 'expires', 'action', 'x', 'y'})
        self.assertRegex(request['id'], '^[0-9a-f]{32}$')
        self.assertEqual(request['session'], 'board-session')
        self.assertEqual(request['action'], 'aim')
        self.assertEqual((request['x'], request['y']), (320, 400))
        self.assertGreaterEqual(request['expires'], started + 1.9)
        self.assertLessEqual(request['expires'], time.time() + 2)
        self.assertFalse((self.data_dir / 'request.tmp').exists())
        game_result = {'id': request['id'], 'ok': True, 'state': {'tick': 101, 'aim_x': 320, 'aim_y': 400}}
        self.respond(game_result)
        reply = client.receive()
        self.assertEqual(reply['id'], 'aim')
        self.assertIs(reply['result']['isError'], False)
        self.assertEqual(json.loads(reply['result']['content'][0]['text']), game_result)
        self.assertEqual(reply['result']['structuredContent'], game_result)
        self.assertFalse((self.data_dir / 'request.json').exists())


    def test_write_actions_require_explicit_armed_and_available_flags(self):
        client = self.client()
        client.initialize()
        for changes in ({'armed': False}, {'armed': 1}, {'armed': 'true'}, {'available': False, 'reason': 'paused'}, {'available': 1}, {'available': None}):
            self.write_state(**changes)
            for name, arguments in (('peggle_aim', {'x': 100, 'y': 100}), ('peggle_fire', {}), ('peggle_flippers', {'pressed': False})):
                self.assert_rejected_without_command(client, name, arguments)

    def test_write_actions_reject_bad_session_or_board_geometry(self):
        client = self.client()
        client.initialize()
        for changes in ({'session': None}, {'session': 1}, {'session': ''}, {'width': 0}, {'width': True}, {'width': 40000}, {'height': -1}, {'height': None}):
            self.write_state(**changes)
            self.assert_rejected_without_command(client, 'peggle_aim', {'x': 100, 'y': 100})

    def test_write_arguments_are_strict_and_within_snapshot_dimensions(self):
        self.write_state()
        client = self.client()
        client.initialize()
        cases = (
            ('peggle_aim', {'x': -1, 'y': 100}),
            ('peggle_aim', {'x': 651, 'y': 100}),
            ('peggle_aim', {'x': 100, 'y': 551}),
            ('peggle_aim', {'x': True, 'y': 100}),
            ('peggle_aim', {'x': '100', 'y': 100}),
            ('peggle_aim', {'x': None, 'y': 100}),
            ('peggle_aim', {'x': 100}),
            ('peggle_aim', {'x': 100, 'y': 100, 'extra': 1}),
            ('peggle_fire', {'x': 100}),
            ('peggle_fire', {'y': 100}),
            ('peggle_fire', {'x': 100, 'y': 551}),
            ('peggle_flippers', {'pressed': 1}),
            ('peggle_flippers', {'pressed': 'false'}),
            ('peggle_flippers', {}),
        )
        for name, arguments in cases:
            self.assert_rejected_without_command(client, name, arguments)


    def test_matching_game_response_must_have_valid_result_fields(self):
        client = self.client()
        client.initialize()
        for fields in ({}, {'ok': 1}, {'ok': 'true'}, {'ok': True, 'state': []}, {'ok': False, 'error': 7}):
            self.write_state()
            client.send({'jsonrpc': '2.0', 'id': 'bad-response', 'method': 'tools/call', 'params': {'name': 'peggle_fire', 'arguments': {}}})
            request = self.await_request()
            self.respond({'id': request['id'], **fields})
            reply = client.receive()['result']
            self.assertIs(reply['isError'], True)
            self.assertIn('response', json.loads(reply['content'][0]['text'])['error'].lower())
            self.assertFalse((self.data_dir / 'request.json').exists())


    def test_fire_and_flippers_forward_the_game_result_in_legacy_text_content(self):
        client = self.client()
        client.initialize('2024-11-05')
        identifiers = set()
        cases = (
            ('peggle_fire', {}, True),
            ('peggle_fire', {'x': 650, 'y': 550}, True),
            ('peggle_flippers', {'pressed': True}, True),
            ('peggle_flippers', {'pressed': False}, False),
            ('peggle_aim', {'x': 0, 'y': 0}, True),
        )
        for name, arguments, ok in cases:
            self.write_state()
            client.send({'jsonrpc': '2.0', 'id': name, 'method': 'tools/call', 'params': {'name': name, 'arguments': arguments}})
            request = self.await_request()
            identifiers.add(request['id'])
            self.assertEqual(request['action'], name.removeprefix('peggle_'))
            self.assertEqual({key: value for key, value in request.items() if key not in ('id', 'session', 'expires', 'action')}, arguments)
            response = {'id': request['id'], 'ok': ok, 'state': {'tick': 102, 'score': 1234}}
            if not ok:
                response['error'] = 'flippers unavailable'
            self.respond(response)
            result = client.receive()['result']
            self.assertEqual(result['isError'], not ok)
            self.assertEqual(json.loads(result['content'][0]['text']), response)
            self.assertNotIn('structuredContent', result)
            self.assertFalse((self.data_dir / 'request.json').exists())
        self.assertEqual(len(identifiers), len(cases))

    def test_timeout_deletes_only_its_request_without_retrying_fire(self):
        self.write_state()
        client = self.client()
        client.initialize()
        started = time.monotonic()
        client.send({'jsonrpc': '2.0', 'id': 'fire', 'method': 'tools/call', 'params': {'name': 'peggle_fire', 'arguments': {}}})
        request = self.await_request()
        self.respond({'id': 'unrelated', 'ok': True})
        time.sleep(0.1)
        self.assertEqual(json.loads((self.data_dir / 'request.json').read_text(encoding='utf-8')), request)
        result = client.receive()['result']
        self.assertGreaterEqual(time.monotonic() - started, 1.9)
        self.assertLess(time.monotonic() - started, 3.5)
        self.assertIs(result['isError'], True)
        self.assertIn('timed out', json.loads(result['content'][0]['text'])['error'])
        self.assertFalse((self.data_dir / 'request.json').exists())
        self.assertFalse((self.data_dir / 'request.tmp').exists())
        self.respond({'id': request['id'], 'ok': True})
        time.sleep(0.1)
        self.assertFalse((self.data_dir / 'request.json').exists())
        self.assertEqual(client.request('ping')['result'], {})

    def test_timeout_preserves_a_replacement_request(self):
        self.write_state()
        client = self.client()
        client.initialize()
        client.send({'jsonrpc': '2.0', 'id': 'fire', 'method': 'tools/call', 'params': {'name': 'peggle_fire', 'arguments': {}}})
        self.await_request()
        replacement = {'id': 'another-request', 'action': 'fire'}
        temporary = self.data_dir / 'replacement.tmp'
        temporary.write_text(json.dumps(replacement), encoding='utf-8')
        os.replace(temporary, self.data_dir / 'request.json')
        self.assertIs(client.receive()['result']['isError'], True)
        self.assertEqual(json.loads((self.data_dir / 'request.json').read_text(encoding='utf-8')), replacement)

    def test_existing_request_is_not_overwritten(self):
        self.write_state()
        request = {'id': 'previous', 'expires': time.time() + 30, 'action': 'fire'}
        path = self.data_dir / 'request.json'
        path.write_text(json.dumps(request), encoding='utf-8')
        client = self.client()
        client.initialize()
        result = self.call(client, 'peggle_fire')['result']
        self.assertIs(result['isError'], True)
        self.assertIn('outstanding', json.loads(result['content'][0]['text'])['error'])
        self.assertEqual(json.loads(path.read_text(encoding='utf-8')), request)

    def test_queued_calls_never_overwrite_an_outstanding_command(self):
        self.write_state()
        client = self.client()
        client.initialize()
        client.send({'jsonrpc': '2.0', 'id': 'first', 'method': 'tools/call', 'params': {'name': 'peggle_fire', 'arguments': {}}})
        first = self.await_request()
        client.send({'jsonrpc': '2.0', 'id': 'second', 'method': 'tools/call', 'params': {'name': 'peggle_flippers', 'arguments': {'pressed': False}}})
        time.sleep(0.05)
        self.assertEqual(json.loads((self.data_dir / 'request.json').read_text(encoding='utf-8')), first)
        self.respond({'id': first['id'], 'ok': True})
        self.assertEqual(client.receive()['id'], 'first')
        second = self.await_request()
        self.assertNotEqual(first['id'], second['id'])
        self.assertEqual(second['action'], 'flippers')
        self.respond({'id': second['id'], 'ok': True})
        self.assertEqual(client.receive()['id'], 'second')

    def test_incomplete_response_is_polled_until_valid_matching_result(self):
        self.write_state()
        client = self.client()
        client.initialize()
        client.send({'jsonrpc': '2.0', 'id': 'fire', 'method': 'tools/call', 'params': {'name': 'peggle_fire', 'arguments': {}}})
        request = self.await_request()
        (self.data_dir / 'response.json').write_text('{', encoding='utf-8')
        time.sleep(0.05)
        response = {'id': request['id'], 'ok': False, 'error': 'focus lost'}
        self.respond(response)
        result = client.receive()['result']
        self.assertIs(result['isError'], True)
        self.assertEqual(json.loads(result['content'][0]['text']), response)


    def test_fractional_coordinates_are_rejected_like_the_game_api(self):
        self.write_state()
        client = self.client()
        client.initialize()
        self.assert_rejected_without_command(client, 'peggle_aim', {'x': 320.5, 'y': 400})
        self.assert_rejected_without_command(client, 'peggle_fire', {'x': 320, 'y': 400.5})
        tools = {tool['name']: tool for tool in client.request('tools/list')['result']['tools']}
        self.assertEqual(tools['peggle_aim']['inputSchema']['properties']['x']['type'], 'integer')


    def test_duplicate_json_fields_are_rejected_in_messages_and_snapshots(self):
        client = self.client()
        client.initialize()
        client.send_raw(b'{"jsonrpc":"2.0","id":9,"method":"ping","method":"tools/call"}\n')
        self.assertEqual(client.receive()['error']['code'], -32700)
        path = self.data_dir / 'state.json'
        path.write_text('{"armed":false,"armed":true}', encoding='utf-8')
        result = self.call(client, 'peggle_observe')['result']
        self.assertIs(result['isError'], True)
        self.assertIn('Duplicate', json.loads(result['content'][0]['text'])['error'])
        self.assertEqual(client.request('ping')['result'], {})


    def test_method_parameters_do_not_silently_enable_unsupported_features(self):
        client = self.client()
        client.initialize()
        for method, params in (
            ('ping', {'unexpected': True}),
            ('tools/list', {'cursor': 'not-a-cursor'}),
            ('tools/list', {'_meta': []}),
            ('tools/call', {'name': 'peggle_fire', 'arguments': {}, 'task': {}}),
            ('tools/call', {'name': 'peggle_observe', 'arguments': {}, '_meta': []}),
        ):
            self.assertEqual(client.request(method, params)['error']['code'], -32602)
        self.assertEqual(client.request('ping', {'_meta': {}})['result'], {})
        self.assertEqual(len(client.request('tools/list', {'_meta': {}})['result']['tools']), 5)
        self.assertFalse((self.data_dir / 'request.json').exists())


    def test_snapshot_read_retries_a_submillisecond_future_mtime_without_accepting_future_data(self):
        state = self.write_state()
        path = self.data_dir / 'state.json'
        timestamp = path.stat().st_mtime
        spec = importlib.util.spec_from_file_location('peggle_mcp_test', SERVER)
        assert spec is not None and spec.loader is not None
        module = importlib.util.module_from_spec(spec)
        with mock.patch.object(sys, 'dont_write_bytecode', True):
            spec.loader.exec_module(module)
        with mock.patch.object(module.time, 'time', side_effect=(timestamp - 0.001, timestamp + 0.001)):
            with mock.patch.object(module.time, 'sleep'):
                self.assertEqual(module.Server(self.data_dir).call_tool('peggle_observe', {}), state)
        self.assertFalse((self.data_dir / 'request.json').exists())

    def test_every_write_action_rechecks_state_freshness(self):
        client = self.client()
        client.initialize()
        for name, arguments in (('peggle_aim', {'x': 100, 'y': 100}), ('peggle_fire', {}), ('peggle_flippers', {'pressed': True})):
            self.write_state()
            timestamp = time.time() - 3
            os.utime(self.data_dir / 'state.json', (timestamp, timestamp))
            self.assert_rejected_without_command(client, name, arguments)


    def test_request_polling_retries_transient_windows_sharing_errors(self):
        path = self.data_dir / 'request.json'
        request = {'id': 'sharing-test', 'action': 'fire'}
        path.write_text(json.dumps(request), encoding='utf-8')
        read_text = Path.read_text
        failures = []

        def transient_read(target, *args, **kwargs):
            if target == path and not failures:
                failures.append(True)
                raise PermissionError('Transient Windows sharing violation')
            return read_text(target, *args, **kwargs)

        with mock.patch.object(Path, 'read_text', transient_read):
            self.assertEqual(self.await_request(), request)
        self.assertEqual(failures, [True])


    def test_request_polling_reports_a_persistent_read_error_at_its_deadline(self):
        with mock.patch.object(Path, 'read_text', side_effect=PermissionError('Persistent sharing violation')):
            with mock.patch.object(time, 'monotonic', side_effect=(0, 0, 4)):
                with self.assertRaisesRegex(AssertionError, 'Persistent sharing violation'):
                    self.await_request()


if __name__ == '__main__':
    unittest.main()
