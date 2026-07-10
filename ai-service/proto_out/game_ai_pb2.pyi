from google.protobuf.internal import containers as _containers
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from typing import ClassVar as _ClassVar, Iterable as _Iterable, Mapping as _Mapping, Optional as _Optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class PromptRequest(_message.Message):
    __slots__ = ("game_id", "difficulty", "past_prompts")
    GAME_ID_FIELD_NUMBER: _ClassVar[int]
    DIFFICULTY_FIELD_NUMBER: _ClassVar[int]
    PAST_PROMPTS_FIELD_NUMBER: _ClassVar[int]
    game_id: str
    difficulty: str
    past_prompts: _containers.RepeatedScalarFieldContainer[str]
    def __init__(self, game_id: _Optional[str] = ..., difficulty: _Optional[str] = ..., past_prompts: _Optional[_Iterable[str]] = ...) -> None: ...

class PromptResponse(_message.Message):
    __slots__ = ("prompt", "category")
    PROMPT_FIELD_NUMBER: _ClassVar[int]
    CATEGORY_FIELD_NUMBER: _ClassVar[int]
    prompt: str
    category: str
    def __init__(self, prompt: _Optional[str] = ..., category: _Optional[str] = ...) -> None: ...

class PlayerSubmission(_message.Message):
    __slots__ = ("player_id", "image_base64")
    PLAYER_ID_FIELD_NUMBER: _ClassVar[int]
    IMAGE_BASE64_FIELD_NUMBER: _ClassVar[int]
    player_id: str
    image_base64: str
    def __init__(self, player_id: _Optional[str] = ..., image_base64: _Optional[str] = ...) -> None: ...

class JudgeRoundRequest(_message.Message):
    __slots__ = ("round_id", "prompt", "submissions")
    ROUND_ID_FIELD_NUMBER: _ClassVar[int]
    PROMPT_FIELD_NUMBER: _ClassVar[int]
    SUBMISSIONS_FIELD_NUMBER: _ClassVar[int]
    round_id: str
    prompt: str
    submissions: _containers.RepeatedCompositeFieldContainer[PlayerSubmission]
    def __init__(self, round_id: _Optional[str] = ..., prompt: _Optional[str] = ..., submissions: _Optional[_Iterable[_Union[PlayerSubmission, _Mapping]]] = ...) -> None: ...

class PlayerResult(_message.Message):
    __slots__ = ("player_id", "score", "rank", "feedback", "confidence")
    PLAYER_ID_FIELD_NUMBER: _ClassVar[int]
    SCORE_FIELD_NUMBER: _ClassVar[int]
    RANK_FIELD_NUMBER: _ClassVar[int]
    FEEDBACK_FIELD_NUMBER: _ClassVar[int]
    CONFIDENCE_FIELD_NUMBER: _ClassVar[int]
    player_id: str
    score: float
    rank: int
    feedback: str
    confidence: float
    def __init__(self, player_id: _Optional[str] = ..., score: _Optional[float] = ..., rank: _Optional[int] = ..., feedback: _Optional[str] = ..., confidence: _Optional[float] = ...) -> None: ...

class JudgeRoundResponse(_message.Message):
    __slots__ = ("results",)
    RESULTS_FIELD_NUMBER: _ClassVar[int]
    results: _containers.RepeatedCompositeFieldContainer[PlayerResult]
    def __init__(self, results: _Optional[_Iterable[_Union[PlayerResult, _Mapping]]] = ...) -> None: ...

class HintRequest(_message.Message):
    __slots__ = ("game_id", "prompt", "time_remaining", "hint_number")
    GAME_ID_FIELD_NUMBER: _ClassVar[int]
    PROMPT_FIELD_NUMBER: _ClassVar[int]
    TIME_REMAINING_FIELD_NUMBER: _ClassVar[int]
    HINT_NUMBER_FIELD_NUMBER: _ClassVar[int]
    game_id: str
    prompt: str
    time_remaining: float
    hint_number: int
    def __init__(self, game_id: _Optional[str] = ..., prompt: _Optional[str] = ..., time_remaining: _Optional[float] = ..., hint_number: _Optional[int] = ...) -> None: ...

class HintResponse(_message.Message):
    __slots__ = ("hint",)
    HINT_FIELD_NUMBER: _ClassVar[int]
    hint: str
    def __init__(self, hint: _Optional[str] = ...) -> None: ...

class FeedbackRequest(_message.Message):
    __slots__ = ("player_id", "prompt", "image_base64", "score")
    PLAYER_ID_FIELD_NUMBER: _ClassVar[int]
    PROMPT_FIELD_NUMBER: _ClassVar[int]
    IMAGE_BASE64_FIELD_NUMBER: _ClassVar[int]
    SCORE_FIELD_NUMBER: _ClassVar[int]
    player_id: str
    prompt: str
    image_base64: str
    score: float
    def __init__(self, player_id: _Optional[str] = ..., prompt: _Optional[str] = ..., image_base64: _Optional[str] = ..., score: _Optional[float] = ...) -> None: ...

class FeedbackResponse(_message.Message):
    __slots__ = ("feedback", "strengths", "improvements")
    FEEDBACK_FIELD_NUMBER: _ClassVar[int]
    STRENGTHS_FIELD_NUMBER: _ClassVar[int]
    IMPROVEMENTS_FIELD_NUMBER: _ClassVar[int]
    feedback: str
    strengths: _containers.RepeatedScalarFieldContainer[str]
    improvements: _containers.RepeatedScalarFieldContainer[str]
    def __init__(self, feedback: _Optional[str] = ..., strengths: _Optional[_Iterable[str]] = ..., improvements: _Optional[_Iterable[str]] = ...) -> None: ...

class HealthCheckRequest(_message.Message):
    __slots__ = ()
    def __init__(self) -> None: ...

class HealthCheckResponse(_message.Message):
    __slots__ = ("serving", "version")
    SERVING_FIELD_NUMBER: _ClassVar[int]
    VERSION_FIELD_NUMBER: _ClassVar[int]
    serving: bool
    version: str
    def __init__(self, serving: bool = ..., version: _Optional[str] = ...) -> None: ...
