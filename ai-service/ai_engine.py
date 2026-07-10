import os
import base64
import random
import logging
import requests
from dotenv import load_dotenv

LANGCHAIN_AVAILABLE = False

load_dotenv()

USE_MOCK_AI = os.getenv("USE_MOCK_AI", "true").lower() == "true"
GROQ_API_KEY = os.getenv("GROQ_API_KEY")

class AIEngine:
    def __init__(self):
        self.mock_mode = USE_MOCK_AI
        if self.mock_mode:
            logging.info("Initializing Mock AI Engine...")
        else:
            logging.info("Initializing Real AI Engine (Pure Groq API)...")

    def _call_gemini_text(self, prompt: str, key: str) -> str:
        url = f"https://generativelanguage.googleapis.com/v1beta/models/gemini-1.5-flash:generateContent?key={key}"
        payload = {"contents": [{"parts": [{"text": prompt}]}]}
        try:
            res = requests.post(url, json=payload, timeout=10)
            if res.status_code == 200:
                data = res.json()
                return data["candidates"][0]["content"]["parts"][0]["text"]
            else:
                logging.error(f"Gemini Text API Error: {res.status_code} - {res.text}")
        except Exception as e:
            logging.error(f"Gemini Text Exception: {e}")
        return ""

    def validate_keys(self, groq_key: str) -> tuple[bool, str]:
        if groq_key:
            res = self._call_gemini_text("Say hello", groq_key)
            if res:
                return True, ""
            return False, "Invalid Gemini API Key or Rate Limit Reached."
        return True, ""

    def get_prompt(self, difficulty: str, past_prompts: list[str], custom_groq_key=None) -> tuple[str, str]:
        key = custom_groq_key or GROQ_API_KEY
        if self.mock_mode or not key:
            return "A flying turtle", "animal"
            
        prompt = f"You are a game master for a drawing game. Generate a single creative prompt and its category. Difficulty: {difficulty}. Do NOT use these: {past_prompts}. Output ONLY in format PROMPT|CATEGORY"
        res = self._call_gemini_text(prompt, key)
        if res and "|" in res:
            parts = res.strip().split("|")
            return parts[0].strip(), parts[1].strip()
        return "A flying turtle", "animal"
        
    def judge_submissions(self, prompt: str, submissions: list[tuple[str, str]], custom_groq_key=None, custom_hf_key=None) -> list[dict]:
        results = []
        key = custom_groq_key or GROQ_API_KEY
        
        if self.mock_mode or not key:
            logging.info("Using mock random scores for judging.")
            for pid, _ in submissions:
                results.append({"player_id": pid, "score": random.uniform(0.5, 0.95), "confidence": 0.9, "rank": 1, "feedback": "Nice job!"})
            return results

        url = f"https://generativelanguage.googleapis.com/v1beta/models/gemini-1.5-flash:generateContent?key={key}"
        for pid, b64_img in submissions:
            score = 0.1
            feedback = "Good effort!"
            try:
                if "," in b64_img: b64_img = b64_img.split(",")[1]
                payload = {
                    "contents": [{
                        "parts": [
                            {"text": f"You are a fun AI judge in a drawing game. The player was supposed to draw: '{prompt}'. Score their drawing from 0.0 to 1.0 based on how well it matches the prompt. Also provide 1 short funny and encouraging sentence of feedback. Output ONLY in JSON format: {{\"score\": 0.85, \"feedback\": \"Wow, that's wild!\"}}"},
                            {"inline_data": {"mime_type": "image/png", "data": b64_img}}
                        ]
                    }],
                    "generationConfig": {"responseMimeType": "application/json"}
                }
                
                res = requests.post(url, json=payload, timeout=15)
                if res.status_code == 200:
                    import json
                    data = res.json()
                    text = data["candidates"][0]["content"]["parts"][0]["text"]
                    parsed = json.loads(text)
                    score = float(parsed.get("score", 0.5))
                    feedback = parsed.get("feedback", "Nice drawing!")
                else:
                    logging.error(f"Gemini API Error: {res.status_code}")
                    score = random.uniform(0.4, 0.9)
            except Exception as e:
                logging.error(f"Error judging image {pid}: {e}")
                score = random.uniform(0.4, 0.9)
                
            results.append({"player_id": pid, "score": score, "confidence": 0.9, "feedback": feedback})
            
        results.sort(key=lambda x: x["score"], reverse=True)
        for rank, res in enumerate(results, start=1):
            res["rank"] = rank
            
        return results

    def get_hint(self, prompt: str, hint_number: int, custom_groq_key=None) -> str:
        key = custom_groq_key or GROQ_API_KEY
        if self.mock_mode or not key:
            return f"Hint #{hint_number} for {prompt}: Try focusing on the main shape."
            
        query = f"Give hint #{hint_number} (short 1 sentence) for someone trying to draw: {prompt}"
        res = self._call_gemini_text(query, key)
        if res:
            return res.strip()
        return "Just draw from the heart!"
