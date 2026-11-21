import argparse
from contextlib import contextmanager
import json
import math
import os
from pathlib import Path
import signal
import sys
import time
import uuid


VERSIONS = ('2025-11-25', '2025-06-18', '2025-03-26', '2024-11-05')
MAX_BYTES = 1024 * 1024
TOOLS = [
    {
        'name': name, 'description': description,
        'inputSchema': {
            'type': 'object', 'properties': properties, 'required': required,
            'additionalProperties': False, **extra,
        },
    }
    for name, description, properties, required, extra in (
        ('peggle_observe', 'Read the fresh board snapshot without controlling the game. Coordinates are board pixels, origin top-left. Peg type: 1 blue, 2 orange (clear these to win), 3 purple, 4 green. Ignore gone or lit pegs. Check armed, available, reason and phase before actions.', {}, [], {}),
        ('peggle_aim', 'Aim at integer board coordinates while the player has armed control.', {'x': {'type': 'integer', 'minimum': 0}, 'y': {'type': 'integer', 'minimum': 0}}, ['x', 'y'], {}),
        ('peggle_fire', 'Record one fire click, optionally aiming first. Success accepts the click; the normal cannon delay may still report phase aim. Observe until launch rather than firing again. Never automatically retry a shot.', {'x': {'type': 'integer', 'minimum': 0}, 'y': {'type': 'integer', 'minimum': 0}}, [], {'oneOf': [{'required': ['x', 'y']}, {'maxProperties': 0}]}),
        ('peggle_flippers', 'Press or release flippers while control is armed.', {'pressed': {'type': 'boolean'}}, ['pressed'], {}),
        ('peggle_wait', 'Wait in real time, then read a fresh snapshot; does not advance a paused game.', {'seconds': {'type': 'number', 'minimum': 0, 'maximum': 10}}, ['seconds'], {}),
    )
]


class RPCError(Exception):
    def __init__(self, code, message):
        super().__init__(message)
        self.code = code


def finite_float(value):
    number = float(value)
    if not math.isfinite(number):
        raise ValueError('Non-finite JSON number')
    return number


def reject_constant(value):
    raise ValueError('Invalid JSON constant: ' + value)


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError('Duplicate JSON field: ' + key)
        result[key] = value
    return result


def parse_json(data):
    return json.loads(data.decode('utf-8'), parse_float=finite_float, parse_constant=reject_constant, object_pairs_hook=unique_object)


def error_response(identifier, code, message):
    return {'jsonrpc': '2.0', 'id': identifier, 'error': {'code': code, 'message': message}}


def read_object(path, fresh=False):
    with path.open('rb') as stream:
        info = os.fstat(stream.fileno())
        if info.st_size > MAX_BYTES:
            raise ValueError(path.name + ' exceeds 1 MiB')
        if fresh and not 0 <= time.time() - info.st_mtime <= 2:
            raise ValueError('state.json is stale or has a future mtime; keep the game running')
        data = stream.read(MAX_BYTES + 1)
    if len(data) > MAX_BYTES:
        raise ValueError(path.name + ' exceeds 1 MiB')
    value = parse_json(data)
    if not isinstance(value, dict):
        raise ValueError(path.name + ' must contain a JSON object')
    return value


def read_state(path):
    deadline = time.monotonic() + 0.05
    while True:
        try:
            return read_object(path, fresh=True)
        except (OSError, ValueError, RecursionError):
            if time.monotonic() >= deadline:
                raise
            time.sleep(0.005)


def bounded_number(value, minimum, maximum, name):
    if type(value) not in (int, float) or not minimum <= value <= maximum or not math.isfinite(value):
        raise ValueError(name + ' must be a finite number between ' + str(minimum) + ' and ' + str(maximum))
    return value


@contextmanager
def data_lock(data_dir):
    data_dir.mkdir(parents=True, exist_ok=True)
    lock = data_dir / 'bridge.lock'
    token = str(os.getpid()) + ':' + uuid.uuid4().hex
    try:
        with lock.open('x', encoding='utf-8') as stream:
            stream.write(token)
    except FileExistsError:
        raise ValueError('bridge.lock already exists. Use one companion per --data-dir; remove the lock only after closing every client using it.') from None
    try:
        yield
    finally:
        try:
            if lock.read_text(encoding='utf-8') == token:
                lock.unlink()
        except OSError:
            pass


class Server:
    def __init__(self, data_dir):
        self.data_dir = data_dir
        self.version = None
        self.ready = False

    def handle(self, message):
        if not isinstance(message, dict) or message.get('jsonrpc') != '2.0' or not isinstance(message.get('method'), str):
            return error_response(None, -32600, 'Invalid request')
        method = message.get('method')
        if 'id' not in message:
            if method == 'notifications/initialized' and self.version and isinstance(message.get('params', {}), dict):
                self.ready = True
            return None
        identifier = message['id']
        if type(identifier) not in (int, str):
            return error_response(None, -32600, 'Invalid request id')
        params = message.get('params', {})
        try:
            if not isinstance(params, dict):
                raise RPCError(-32602, 'Parameters must be an object')
            if '_meta' in params and not isinstance(params['_meta'], dict):
                raise RPCError(-32602, '_meta must be an object')
            result = self.dispatch(method, params)
        except RPCError as error:
            return error_response(identifier, error.code, str(error))
        return {'jsonrpc': '2.0', 'id': identifier, 'result': result}

    def dispatch(self, method, params):
        if method == 'ping':
            if params.keys() - {'_meta'}:
                raise RPCError(-32602, 'ping accepts no arguments')
            return {}
        if method == 'initialize':
            if self.version:
                raise RPCError(-32600, 'Already initialized')
            client = params.get('clientInfo')
            if not isinstance(params.get('protocolVersion'), str) or not isinstance(params.get('capabilities'), dict) or not isinstance(client, dict) or not all(isinstance(client.get(key), str) for key in ('name', 'version')):
                raise RPCError(-32602, 'initialize requires protocolVersion, capabilities and clientInfo')
            requested = params['protocolVersion']
            self.version = requested if requested in VERSIONS else VERSIONS[0]
            return {
                'protocolVersion': self.version,
                'capabilities': {'tools': {'listChanged': False}},
                'serverInfo': {'name': 'peggleport-mcp', 'version': '1.0.0'},
            }
        if method in ('tools/list', 'tools/call') and not self.ready:
            raise RPCError(-32000, 'Initialize and send notifications/initialized first')
        if method == 'tools/list':
            if params.keys() - {'_meta'}:
                raise RPCError(-32602, 'tools/list has one fixed page and accepts no cursor')
            return {'tools': TOOLS}
        if method == 'tools/call':
            if params.keys() - {'name', 'arguments', '_meta'}:
                raise RPCError(-32602, 'tools/call accepts name and arguments only; tasks are not supported')
            name = params.get('name')
            if not isinstance(name, str) or name not in {tool['name'] for tool in TOOLS}:
                raise RPCError(-32602, 'Unknown tool')
            arguments = params.get('arguments', {})
            if not isinstance(arguments, dict):
                raise RPCError(-32602, 'Tool arguments must be an object')
            try:
                value = self.call_tool(name, arguments)
                return self.tool_result(value, failed=value.get('ok') is False)
            except (OSError, ValueError, RecursionError) as error:
                return self.tool_result({'ok': False, 'error': str(error)}, failed=True)
        raise RPCError(-32601, 'Unknown method: ' + method)

    def call_tool(self, name, arguments):
        schema = next(tool['inputSchema'] for tool in TOOLS if tool['name'] == name)
        if arguments.keys() - schema['properties'].keys() or set(schema['required']) - arguments.keys():
            raise ValueError('Unexpected or missing tool arguments')
        if name == 'peggle_wait':
            seconds = bounded_number(arguments.get('seconds'), 0, 10, 'seconds')
            time.sleep(seconds)
        state = read_state(self.data_dir / 'state.json')
        if name in ('peggle_observe', 'peggle_wait'):
            return state
        if state.get('armed') is not True:
            raise ValueError('Control is not armed; the player must press F8 in the game')
        if state.get('available') is not True:
            raise ValueError('Game control is unavailable: ' + str(state.get('reason', 'blocked')))
        if not isinstance(state.get('session'), str) or not state['session']:
            raise ValueError('Snapshot has no valid session')
        width = bounded_number(state.get('width'), 1, 32768, 'snapshot width')
        height = bounded_number(state.get('height'), 1, 32768, 'snapshot height')
        if ('x' in arguments) != ('y' in arguments):
            raise ValueError('x and y must be provided together')
        if 'x' in arguments:
            bounded_number(arguments['x'], 0, width, 'x')
            bounded_number(arguments['y'], 0, height, 'y')
            if any(value != math.floor(value) for value in (arguments['x'], arguments['y'])):
                raise ValueError('x and y must be integer board coordinates')
        if name == 'peggle_flippers' and type(arguments.get('pressed')) is not bool:
            raise ValueError('pressed must be a boolean')
        return self.command(name.removeprefix('peggle_'), arguments, state)

    def command(self, action, arguments, state):
        request = {
            'id': uuid.uuid4().hex, 'session': state['session'],
            'expires': time.time() + 2, 'action': action, **arguments,
        }
        path = self.data_dir / 'request.json'
        temporary = self.data_dir / 'request.tmp'
        if path.exists():
            raise ValueError('A request.json is already outstanding; no command was sent')
        deadline = time.monotonic() + 2
        try:
            temporary.write_text(json.dumps(request, allow_nan=False), encoding='utf-8')
            os.replace(temporary, path)
            while time.monotonic() < deadline:
                try:
                    response = read_object(self.data_dir / 'response.json')
                except (OSError, ValueError, RecursionError):
                    response = {}
                if response.get('id') == request['id']:
                    if type(response.get('ok')) is not bool or ('error' in response and not isinstance(response['error'], str)) or ('state' in response and not isinstance(response['state'], dict)):
                        raise ValueError('Invalid game response: expected boolean ok, optional string error and object state')
                    return response
                time.sleep(0.01)
            raise ValueError('Game response timed out; the action may have happened. Observe before deciding what to do; do not automatically retry.')
        finally:
            try:
                temporary.unlink(missing_ok=True)
                if read_object(path).get('id') == request['id']:
                    path.unlink()
            except (OSError, ValueError, RecursionError):
                pass

    def tool_result(self, value, failed=False):
        result = {
            'content': [{'type': 'text', 'text': json.dumps(value, allow_nan=False)}],
            'isError': failed,
        }
        if self.version in VERSIONS[:2]:
            result['structuredContent'] = value
        return result


def serve(server):
    while True:
        line = sys.stdin.buffer.readline(MAX_BYTES + 1)
        if not line:
            break
        try:
            if len(line) > MAX_BYTES:
                while line and not line.endswith(b'\n'):
                    line = sys.stdin.buffer.readline(MAX_BYTES + 1)
                raise ValueError('Message exceeds 1 MiB')
            message = parse_json(line)
            response = server.handle(message)
        except (ValueError, RecursionError) as error:
            response = error_response(None, -32700, 'Parse error: ' + str(error))
        if response is not None:
            sys.stdout.write(json.dumps(response, allow_nan=False) + '\n')
            sys.stdout.flush()
    return 0


def main():
    parser = argparse.ArgumentParser(description='PegglePort local-file MCP stdio companion')
    parser.add_argument('--data-dir', required=True, type=Path)
    args = parser.parse_args()
    if not args.data_dir.is_absolute():
        parser.error('--data-dir must be an absolute path to userdata/mod-data/mcp_agent')
    signal.signal(signal.SIGTERM, lambda signum, frame: sys.exit(0))
    try:
        with data_lock(args.data_dir):
            return serve(Server(args.data_dir))
    except (OSError, ValueError) as error:
        print(str(error), file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        return 0


if __name__ == '__main__':
    sys.exit(main())
