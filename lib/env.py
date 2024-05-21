from enum import Enum
from typing import Optional, List

class EnvVarOp (Enum):
    PREPEND=1
    APPEND=2
    SET=3

class EnvVarKind (Enum):
    SCALAR=2
    LIST=2

list_variables = {
        "ACLOCAL_PATH",
        "CMAKE_PREFIX_PATH",
        "CPATH",
        "LD_LIBRARY_PATH",
        "LIBRARY_PATH",
        "MANPATH",
        "PATH",
        "PKG_CONFIG_PATH",
        }

class EnvVarError(Exception):
    """Exception raised when there is an error with environment variable manipulation."""

    def __init__(self, message):
        self.message = message
        super().__init__(self.message)

    def __str__(self):
        return self.message

def is_env_value_list(v):
    return isinstance(v, list) and all(isinstance(item, str) for item in v)

class ListEnvVarUpdate():
    def __init__(self, value: List[str], op: EnvVarOp):
        # strip white space from each entry
        self._value = [v.strip() for v in value]
        self._op = op

    @property
    def op(self):
        return self._op

    @property
    def value(self):
        return self._value

    def __repr__(self):
        return f"env.ListEnvVarUpdate({self.value}, {self.op})"

    def __str__(self):
        return f"({self.value}, {self.op})"

class EnvVar:
    def __init__(self, name: str):
        self._name = name

    @property
    def name(self):
        return self._name

class ListEnvVar(EnvVar):
    def __init__(self, name: str, value: List[str], op: EnvVarOp):
        super().__init__(name)

        self._updates = [ListEnvVarUpdate(value, op)]

    def update(self, value: List[str], op:EnvVarOp):
        self._updates.append(ListEnvVarUpdate(value, op))

    @property
    def updates(self):
        return self._updates

    def concat(self, other: 'ListEnvVar'):
        self._updates += other.updates

    # Given the current value, return the value that should be set
    def get_value(self, current: Optional[str]):
        v = current

        # if the variable is currently not set, first initialise it as empty.
        if v is None:
            if len(self._updates)==0:
                return None
            v = ""

        for update in self._updates:
            joined = ":".join(update.value)
            if v == "" or update.op==EnvVarOp.SET:
                v = joined
            elif update.op==EnvVarOp.APPEND:
                v = ":".join([v, joined])
            elif update.op==EnvVarOp.PREPEND:
                v = ":".join([joined, v])
            else:
                raise EnvVarError(f"Internal error: implement the operation {update.op}");
            # strip any leading/trailing ":"
            v = v.strip(':')

        return v

    def __repr__(self):
        return f"env.ListEnvVar(\"{self.name}\", {self._updates})"

    def __str__(self):
        return f"(\"{self.name}\": [{','.join([str(u) for u in self._updates])}])"


class ScalarEnvVar(EnvVar):
    def __init__(self, name: str, value: Optional[str]):
        super().__init__(name)
        self._value = value

    @property
    def value(self):
        return self._value

    @property
    def is_null(self):
        return self.value is None

    def update(self, value: Optional[str]):
        self._value = value

    def __repr__(self):
        return f"env.ScalarEnvVar(\"{self.name}\", \"{self.value}\")"

    def __str__(self):
        return f"(\"{self.name}\": \"{self.value}\")"

class Env:
    def __init__(self):
        self._vars = {}

    def apply(self, var: EnvVar):
        self._vars[var.name] = var

# returns true if the environment variable with name is a list variable,
# e.g. PATH, LD_LIBRARY_PATH, PKG_CONFIG_PATH, etc.
def is_list_var(name: str) -> bool:
    return name in list_variables

class Env:

    def __init__(self):
        self._lists = {}
        self._scalars = {}

    @property
    def lists(self):
        return self._listself._lists

    @property
    def scalars(self):
        return self._scalars

    def set_scalar(self, var: ScalarEnvVar):
        self._scalars[var.name] = var

    def set_list(self, var: ListEnvVar):
        if var.name in self._lists.keys():
            old = self._lists[var.name]
            self._lists[var.name] = old.concat(var)
        else:
            self._lists[var.name] = var
