import re

def is_full_sha256(s: str):
    pattern = re.compile(r'^[a-fA-F-0-9]{64}$')
    return True if pattern.match(s) else False

def is_id(s: str):
    pattern = re.compile(r'^[a-fA-F-0-9]{16}$')
    return True if pattern.match(s) else False

def is_valid_sha(sha:str) -> bool:
    if is_full_sha256(sha):
        return True
    if is_id(sha):
        return True
    return False

# return dictionary {"name", "version", "tag", "sha"} from a uenv description string
#       "prgenv_gnu"              -> ("prgenv_gnu", None,    None,     None)
#       "prgenv_gnu/23.11"        -> ("prgenv_gnu", "23.11", None,     None)
#       "prgenv_gnu/23.11:latest" -> ("prgenv_gnu", "23.11", "latest", None)
#       "prgenv_gnu:v2"           -> ("prgenv_gnu", None,    "v2",     None)
#       "3313739553fe6553"        -> (None,         None,    None,     "3313739553fe6553")
def parse_uenv_string(uenv: str) -> dict:
    name = version = tag = sha = None

    if is_valid_sha(uenv):
        sha = uenv
    else:
        # todo: assert no more than 1 '/'
        # todo: assert no more than 1 ':'
        # todo: assert that '/' is before ':'
        splits = uenv.split("/",1)
        if len(splits)>1:
            name = splits[0]
            splits = splits[1].split(":",1)
            version = splits[0]
            if len(splits)>1:
                tag = splits[1]
        else:
            splits =  uenv.split(":",1)
            name = splits[0]
            if len(splits)>1:
                tag = splits[1]

    return {"name": name, "version": version, "tag": tag, "sha": sha}

def is_complete_description(uenv: dict) -> bool:
    if uenv["sha"]:
        return True

    if uenv["name"] and uenv["version"] and uenv["tag"]:
        return True

    return False

class IncompleteUenvName(Exception):
    """Exception raised for errors related to invalid uenv names."""

    def __init__(self, message):
        self.message = message
        super().__init__(self.message)

    def __str__(self):
        return f'IncompleteUenvName: {self.message}'


def create_filter(uenv, require_complete=False) -> dict:
    if uenv is None:
        if require_complete:
            raise IncompleteUenvName(f"{uenv}")
        return {}

    components = parse_uenv_string(uenv)
    if require_complete and not is_complete_description(components):
        raise IncompleteUenvName(f"{uenv}")

    img_filter = {}
    for key, value in components.items():
        if value is not None:
            img_filter[key] = value

    return img_filter
