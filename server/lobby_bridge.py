import asyncio
import ipaddress
import logging
import signal
from http import HTTPStatus

from websockets.asyncio.server import serve
from websockets.exceptions import ConnectionClosed

_active = set()


def source_header(connection, request):
    peer = connection.remote_address
    if not peer or not ipaddress.ip_address(peer[0]).is_loopback:
        raise ValueError
    values = request.headers.get_all('CF-Connecting-IP')
    if len(values) != 1 or len(values[0]) > 45 or '%' in values[0]:
        raise ValueError
    source = ipaddress.ip_address(values[0])
    family = 'TCP4' if source.version == 4 else 'TCP6'
    destination = '127.0.0.1' if source.version == 4 else '::ffff:127.0.0.1'
    return f'PROXY {family} {source} {destination} {peer[1]} 7770\r\n'.encode('ascii')


def process_request(connection, request):
    if request.path == '/health':
        return connection.respond(HTTPStatus.OK, 'bridge ready; backend not checked\n')
    if request.path != '/lobby':
        return connection.respond(HTTPStatus.NOT_FOUND, 'not found\n')
    try:
        source_header(connection, request)
    except (ValueError, TypeError, IndexError):
        return connection.respond(HTTPStatus.BAD_REQUEST, 'invalid source address\n')
    if len(_active) >= 256:
        return connection.respond(HTTPStatus.SERVICE_UNAVAILABLE, 'bridge busy\n')
    return None


async def relay(connection):
    if len(_active) >= 256:
        await connection.close(1013, 'bridge busy')
        return
    _active.add(connection)
    writer = None
    tasks = []
    try:
        header = source_header(connection, connection.request)
        reader, writer = await asyncio.wait_for(asyncio.open_connection('127.0.0.1', 7770, limit=65536), 5)
        writer.transport.set_write_buffer_limits(high=32768, low=8192)
        writer.write(header)
        await asyncio.wait_for(writer.drain(), 10)

        async def to_tcp():
            async for data in connection:
                if not isinstance(data, bytes):
                    await connection.close(1003, 'binary messages required')
                    return
                writer.write(data)
                await asyncio.wait_for(writer.drain(), 10)
            if writer.can_write_eof():
                writer.write_eof()
                await asyncio.wait_for(writer.drain(), 3)

        async def to_ws():
            while True:
                data = await reader.read(16384)
                if not data:
                    await connection.close(1000)
                    return
                await asyncio.wait_for(connection.send(data), 10)

        tasks = [asyncio.create_task(to_tcp()), asyncio.create_task(to_ws())]
        done, pending = await asyncio.wait(tasks, return_when=asyncio.FIRST_COMPLETED)
        for task in done:
            task.result()
    except ConnectionClosed:
        pass
    except (OSError, asyncio.TimeoutError, ValueError, TypeError, IndexError):
        await connection.close(1011, 'lobby connection unavailable')
    finally:
        for task in tasks:
            if not task.done():
                task.cancel()
        if tasks:
            await asyncio.gather(*tasks, return_exceptions=True)
        if writer is not None:
            writer.close()
            try:
                await asyncio.wait_for(writer.wait_closed(), 3)
            except (OSError, asyncio.TimeoutError):
                writer.transport.abort()
        _active.discard(connection)


async def main():
    stopped = asyncio.Event()
    loop = asyncio.get_running_loop()
    for sig in (signal.SIGINT, signal.SIGTERM):
        loop.add_signal_handler(sig, stopped.set)
    logger = logging.getLogger('lobby.bridge')
    logger.addHandler(logging.NullHandler())
    logger.propagate = False
    async with serve(relay, '127.0.0.1', 7772, process_request=process_request,
                     compression=None, max_size=1048576, max_queue=4, write_limit=32768,
                     open_timeout=5, ping_interval=20, ping_timeout=20, close_timeout=3,
                     server_header=None, logger=logger):
        await stopped.wait()


if __name__ == '__main__':
    asyncio.run(main())
