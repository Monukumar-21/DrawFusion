import sys, os
sys.path.append(os.path.join(os.path.dirname(__file__), 'proto_out'))
import logging
import grpc

from proto_out import game_ai_pb2
from proto_out import game_ai_pb2_grpc
from ai_engine import AIEngine

class GameMasterService(game_ai_pb2_grpc.GameMasterServicer):
    def __init__(self):
        super().__init__()
        self.engine = AIEngine()
        logging.info("GameMasterService initialized.")

    def ValidateKeys(self, request, context):
        logging.info("Received ValidateKeys request")
        is_valid, err = self.engine.validate_keys(request.groq_key)
        return game_ai_pb2.ValidateKeysResponse(is_valid=is_valid, error_message=err)

    def GetPrompt(self, request, context):
        logging.info(f"Received GetPrompt: game_id={request.game_id}, difficulty={request.difficulty}")
        prompt, category = self.engine.get_prompt(
            request.difficulty, 
            list(request.past_prompts),
            custom_groq_key=request.custom_groq_key
        )
        return game_ai_pb2.PromptResponse(prompt=prompt, category=category)

    def JudgeRound(self, request, context):
        logging.info(f"Received JudgeRound: round_id={request.round_id}, prompt='{request.prompt}' with {len(request.submissions)} submissions")
        
        subs = [(sub.player_id, sub.image_base64) for sub in request.submissions]
        results = self.engine.judge_submissions(
            request.prompt, 
            subs,
            custom_groq_key=request.custom_groq_key
        )
        
        response = game_ai_pb2.JudgeRoundResponse()
        for res in results:
            pr = response.results.add()
            pr.player_id = res["player_id"]
            pr.score = res["score"]
            pr.rank = res["rank"]
            pr.feedback = res["feedback"]
            pr.confidence = res["confidence"]
            
        logging.info("Judging complete.")
        return response

    def GetHint(self, request, context):
        hint = self.engine.get_hint(
            request.prompt, 
            request.hint_number,
            custom_groq_key=request.custom_groq_key
        )
        return game_ai_pb2.HintResponse(hint=hint)

    def GetFeedback(self, request, context):
        return game_ai_pb2.FeedbackResponse(
            feedback=f"Good effort on '{request.prompt}'.",
            strengths=["Lines", "Color"],
            improvements=["Shading"]
        )

    def HealthCheck(self, request, context):
        return game_ai_pb2.HealthCheckResponse(serving=True, version="1.0.0")
