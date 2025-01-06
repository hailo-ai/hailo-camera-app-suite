from typing import List
from pydantic import BaseModel



class StrToReplace(BaseModel):
    file: str
    src: str
    to: str

class RegexRules(BaseModel):
    file: str
    src: str
    to: str
    num_of_appears: int


class ReleaseRules(BaseModel):
    rm_dirs : List[str]
    rm_files: List[str]
    str_replacement: List[StrToReplace]
    regex_rules: List[RegexRules]