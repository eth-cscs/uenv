from enum import Enum
from typing import Optional

class EnvVarOp (Enum):
    PREPEND=1
    APPEND=2
    SET=3

class EnvVarKind (Enum):
    SCALAR=2
    LIST=2

def is_env_value_list(v):
    return isinstance(v, list) and all(isinstance(item, str) for item in v)

class ListEnvVarUpdate():
    def __init__(self, value: list[str], op: EnvVarOp):
        self._value = value
        self._op = op

    @property
    def op(self):
        return self._op

    @property
    def value(self):
        return self._value

class EnvVar:
    def __init__(self, name: str):
        self._name = name

    @property
    def name(self):
        return self._name

class ListEnvVar(EnvVar):
    def __init__(self, name: str, value: list[str], op: EnvVarOp):
        super().__init__(name)

        self._updates = [ListEnvVarUpdate(value, op)]

    def update(self, value: list[str], op:EnvVarOp):
        self._updates.append(ListEnvVarUpdate(value, op))

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

class Env:
    def __init__(self):
        self._vars = {}

    def apply(self, var: EnvVar):
        self._vars[var.name] = var

    def print(self):
        for name, var in self._vars.items():
            print(f"{name}: {var.value}")

env = Env()

s = ScalarEnvVar("CUDA_HOME", "/opt/nvidia/cuda12")
#l = ListEnvVar("PATH", ["/usr/bin", "/opt/nvidia/cuda12/bin"], EnvVarOp.APPEND)

env.apply(s)
#env.apply(l)
env.print()
