import sys, os
sys.path.append(os.path.join(os.path.dirname(__file__), 'proto_out'))
import asyncio
import logging
import grpc
from concurrent import futures

from proto_out import game_ai_pb2
from proto_out import game_ai_pb2_grpc
from game_master import GameMasterService

logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s | %(levelname)s | %(message)s',
    datefmt='%H:%M:%S'
)

async def serve():
    # Setup gRPC server with keepalive options to match C++ client
    options = (
        ('grpc.keepalive_time_ms', 30000),
        ('grpc.keepalive_timeout_ms', 10000),
        ('grpc.keepalive_permit_without_calls', True),
        ('grpc.http2.max_pings_without_data', 0),
        ('grpc.http2.min_time_between_pings_ms', 10000),
        ('grpc.http2.min_ping_interval_without_data_ms', 5000),
    )
    server = grpc.aio.server(
        futures.ThreadPoolExecutor(max_workers=10),
        options=options
    )
    game_ai_pb2_grpc.add_GameMasterServicer_to_server(GameMasterService(), server)
    
    listen_addr = '[::]:50051'
    server.add_insecure_port(listen_addr)
    
    logging.info(f"🚀 AI Game Master Service listening on {listen_addr}")
    await server.start()
    
    # Graceful shutdown handler
    try:
        await server.wait_for_termination()
    except KeyboardInterrupt:
        logging.info("Shutting down AI service...")
        await server.stop(0)

if __name__ == '__main__':
    asyncio.run(serve())
