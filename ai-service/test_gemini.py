import requests
import json
import os
from dotenv import load_dotenv

load_dotenv()
key = os.getenv("GROQ_API_KEY") # I will just pass my own test Gemini Key via environment or manually if I had one.
# Wait, I don't have a Gemini API key.
