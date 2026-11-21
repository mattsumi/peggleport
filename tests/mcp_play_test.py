import json
from pathlib import Path
import queue
import subprocess
import sys
import tempfile
import threading
import time

from test_mcp_server import Client


def main():
    executable, deluxe, nights, scratch, mod_folder = sys.argv[1:]
    with tempfile.TemporaryDirectory(prefix='mcp-play-', dir=scratch) as temporary:
        root = Path(temporary)
        userdata = root / 'userdata'
        errors = (root / 'host.log').open('w', encoding='utf-8')
        host = subprocess.Popen(
            [executable, deluxe, nights, str(root), '--serve', str(userdata), mod_folder],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=errors,
            text=True, encoding='utf-8',
        )
        input_stream, output_stream = host.stdin, host.stdout
        assert input_stream is not None and output_stream is not None
        lines = queue.Queue()
        reader = threading.Thread(target=lambda: [lines.put(line.rstrip()) for line in output_stream], daemon=True)
        reader.start()
        client = None

        def marker(expected):
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline:
                line = lines.get(timeout=max(0.01, deadline - time.monotonic()))
                if line == expected:
                    return
                if line in ('STOPPED', 'DISARMED', 'ARMED', 'READY'):
                    raise AssertionError(f'Expected {expected}, received {line}')
            raise AssertionError(f'Native host did not report {expected}')

        def call(name, arguments=None):
            assert client is not None
            response = client.request('tools/call', {'name': name, 'arguments': arguments or {}})
            if 'error' in response:
                raise AssertionError(response)
            result = response['result']
            return result, json.loads(result['content'][0]['text'])

        def observe(predicate):
            deadline = time.monotonic() + 8
            last = None
            while time.monotonic() < deadline:
                result, last = call('peggle_observe')
                if not result['isError'] and predicate(last):
                    return last
                time.sleep(0.03)
            raise AssertionError(f'Expected native board state not reached: {last}')

        try:
            marker('READY')
            client = Client(userdata / 'mod-data' / 'mcp_agent')
            assert client.initialize()['result']['protocolVersion'] == '2025-11-25'
            tools = client.request('tools/list')['result']['tools']
            assert len(tools) == 5
            initial = observe(lambda state: state.get('armed') is False and state.get('phase') == 'aim')
            assert initial['pegs'] and initial['width'] == 650 and initial['height'] == 560
            result, denied = call('peggle_fire')
            assert result['isError'] and denied['ok'] is False
            input_stream.write('arm\n')
            input_stream.flush()
            marker('ARMED')
            armed = observe(lambda state: state.get('armed') is True and state.get('available') is True)
            result, aimed = call('peggle_aim', {'x': 200, 'y': 400})
            assert not result['isError'] and aimed['ok'] is True
            assert (aimed['state']['aim_x'], aimed['state']['aim_y']) == (200, 400)
            result, fired = call('peggle_fire')
            assert not result['isError'] and fired['ok'] is True
            shot = observe(lambda state: state.get('phase') == 'shot' and bool(state.get('balls')))
            assert shot['session'] == armed['session'] and shot['tick'] > armed['tick']
            result, waited = call('peggle_wait', {'seconds': 0.15})
            assert not result['isError'], waited
            advanced = observe(lambda state: state.get('tick', 0) > shot['tick'])
            input_stream.write('arm\n')
            input_stream.flush()
            marker('DISARMED')
            observe(lambda state: state.get('armed') is False)
            result, denied = call('peggle_aim', {'x': 300, 'y': 400})
            assert result['isError'] and denied['ok'] is False
            assert not (userdata / 'mod-data' / 'mcp_agent' / 'request.json').exists()
            print(f"MCP_PLAY tools={len(tools)} pegs={len(initial['pegs'])} tick={initial['tick']}->{advanced['tick']} real_shot_balls={len(shot['balls'])} disarmed_fire=blocked F8_revoke=blocked")
        finally:
            if client is not None:
                client.close()
                assert client.process.returncode == 0
            if host.poll() is None:
                input_stream.write('stop\n')
                input_stream.flush()
                try:
                    host.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    host.kill()
                    host.wait(timeout=5)
            input_stream.close()
            reader.join(timeout=5)
            output_stream.close()
            errors.close()
            if host.returncode:
                raise AssertionError((root / 'host.log').read_text(encoding='utf-8'))
    return 0


if __name__ == '__main__':
    sys.exit(main())
