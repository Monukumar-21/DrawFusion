import os
import requests
from dotenv import load_dotenv

load_dotenv()
key = os.getenv("GROQ_API_KEY")

headers = {"Authorization": f"Bearer {key}"}
res = requests.get("https://api.groq.com/openai/v1/models", headers=headers)
models = res.json().get("data", [])
for m in models:
    if "vision" in m["id"].lower():
        print("VISION MODEL:", m["id"])
