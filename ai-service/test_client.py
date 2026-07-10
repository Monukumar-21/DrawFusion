import sys, os
sys.path.append(os.path.join(os.path.dirname(__file__), 'proto_out'))
import grpc
import logging
from proto_out import game_ai_pb2
from proto_out import game_ai_pb2_grpc

logging.basicConfig(level=logging.INFO)

def run():
    logging.info("Connecting to AI Service on localhost:50051...")
    
    with grpc.insecure_channel('localhost:50051') as channel:
        stub = game_ai_pb2_grpc.GameMasterStub(channel)
        
        # 1. HealthCheck
        logging.info("--- HealthCheck ---")
        try:
            res = stub.HealthCheck(game_ai_pb2.HealthCheckRequest())
            logging.info(f"HealthCheck serving: {res.serving}, version: {res.version}")
        except Exception as e:
            logging.error(f"HealthCheck failed: {e}")
            return
            
        # 2. GetPrompt
        logging.info("--- GetPrompt ---")
        prompt_res = stub.GetPrompt(game_ai_pb2.PromptRequest(game_id="test1", difficulty="medium", past_prompts=[]))
        logging.info(f"Got Prompt: '{prompt_res.prompt}' (Category: {prompt_res.category})")
        
        # 3. JudgeRound (with mock empty image)
        logging.info("--- JudgeRound ---")
        # We'll just pass a tiny valid base64 transparent 1x1 png for testing CLIP
        b64_img = "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNkYAAAAAYAAjCB0C8AAAAASUVORK5CYII="
        
        req = game_ai_pb2.JudgeRoundRequest(
            round_id="round_1",
            prompt=prompt_res.prompt,
            submissions=[
                game_ai_pb2.PlayerSubmission(player_id="player1", image_base64=b64_img),
                game_ai_pb2.PlayerSubmission(player_id="player2", image_base64=b64_img)
            ]
        )
        judge_res = stub.JudgeRound(req)
        for res in judge_res.results:
            logging.info(f"Player: {res.player_id} | Rank: {res.rank} | Score: {res.score:.2f} | Feedback: {res.feedback}")
            
if __name__ == '__main__':
    run()
