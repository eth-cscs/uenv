from datetime import datetime, timezone

class Record:
    @staticmethod
    def to_datetime(date: str):
        # In Python 3.6, datetime.fromisoformat is not available.
        # Manually parsing the string.
        dt_format = '%Y-%m-%dT%H:%M:%S.%fZ'
        return datetime.strptime(date, dt_format).replace(tzinfo=timezone.utc)


    def __init__(self, system: str, uarch: str, name: str, version: str, tag: str, date: str, size_bytes: int, sha256: str):
        self._system  = system
        self._uarch   = uarch
        self._name    = name
        self._version = version
        self._tag     = tag
        self._date    = date
        self._bytes   = size_bytes
        self._sha256  = sha256
        self._id      = sha256[:16]

    # build/eiger/zen2/cp2k/2023/1133706947
    @classmethod
    def frompath(cls, path: str, date: str, size_bytes: int, sha256: str):
        fields = path.split("/")
        if len(fields) != 5:
            raise ValueError("Record must have exactly 5 fields")

        system, uarch, name, version, tag = fields
        return cls(system, uarch, name, version, tag, date, size_bytes, sha256)

    def __eq__(self, other):
        if not isinstance(other, Record):
            return False
        return self.sha256==other.sha256

    def __lt__(self, other):
        if self.system  < other.system: return True
        if other.system < self.system: return False
        if self.uarch   < other.uarch: return True
        if other.uarch  < self.uarch: return False
        if self.name    < other.name: return True
        if other.name   < self.name: return False
        if self.version < other.version: return True
        if other.version< self.version: return False
        if self.tag     < other.tag: return True
        if other.tag    < self.tag: return False
        if self.id      < other.id: return True
        if other.id     < self.id: return False
        return False

    def __str__(self):
        return f"{self.system}/{self.uarch}/{self.name}/{self.version}:{self.tag}/{self.sha256}"

    def __repr__(self):
        return f"Record({self.system}, {self.uarch}, {self.name}, {self.version}, {self.tag}, {self.sha256[:16]})"

    @property
    def system(self):
        return self._system

    @property
    def uarch(self):
        return self._uarch

    @property
    def name(self):
        return self._name

    @property
    def date(self):
        return self._date

    @property
    def version(self):
        return self._version

    @property
    def tag(self):
        return self._tag

    @tag.setter
    def tag(self, newtag):
        self._tag = newtag

    @property
    def sha256(self):
        return self._sha256

    @property
    def id(self):
        return self._id

    @property
    def size(self):
        return self._bytes

    @property
    def path(self):
        return f"{self.system}/{self.uarch}/{self.name}{self.version}/{self.tag}"

    @property
    def dictionary(self):
        return {
                "system": self.system,
                "uarch": self.uarch,
                "name": self.name,
                "date": self.datestring,
                "version": self.version,
                "tag": self.tag,
                "sha256": self.sha256,
                "id": self.id,
                "size": self.size
            }

    @property
    def full_name(self):
        return f"{self.name}/{self.version}:{self.tag}"
