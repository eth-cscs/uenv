import json
import os

from record import Record
import terminal
import names

UENV_CLI_API_VERSION=1

class DataStore:

    def __init__(self):
        # all images store with (key,value) = (sha256,Record)
        self._images = {}
        self._short_sha = {}

        self._store = {"system": {}, "uarch": {}, "name": {}, "version": {}, "tag": {}}

    def add_record(self, r: Record):
        sha = r.sha256
        short_sha = r.sha256[:16]
        self._images.setdefault(sha, []).append(r)
        # check for (exceedingly unlikely) collision
        if short_sha in self._short_sha:
            old_sha = self._short_sha[short_sha]
            if sha != old_sha:
                terminal.error('image hash collision:\n  {sha}\n  {old_sha}')
        self._short_sha[sha[:16]] = sha
        self._store["system"] .setdefault(r.system, []).append(sha)
        self._store["uarch"]  .setdefault(r.uarch, []).append(sha)
        self._store["name"]   .setdefault(r.name, []).append(sha)
        self._store["version"].setdefault(r.version, []).append(sha)
        self._store["tag"]    .setdefault(r.tag, []).append(sha)

    def find_records(self, **constraints):
        if not constraints:
            raise ValueError("At least one constraint must be provided")

        for field in constraints:
            if (field != "sha") and (field not in self._store):
                raise ValueError(f"Invalid field: {field}. Must be one of 'system', 'uarch', 'name', 'version', 'tag', 'sha'")

        if "sha" in constraints:
            sha = constraints["sha"]
            matching_records_sets = [set()]
            if len(sha)<64:
                if sha in self._short_sha:
                    matching_records_sets = [set([self._short_sha[sha]])]
            else:
                if sha in self._images:
                    matching_records_sets = [set([sha])]
        else:
            # Find matching records for each constraint
            matching_records_sets = [
                set(self._store[field].get(value, [])) for field, value in constraints.items()
            ]

        # Intersect all sets of matching records
        if matching_records_sets:
            unique = set.intersection(*matching_records_sets)
        else:
            unique = set()

        results = []
        for sha in unique:
            results += (self._images[sha])
        results.sort(reverse=True)
        return results

    @property
    def images(self):
        return self._images

    # return a list of records that match a sha
    def get_record(self, sha: str) -> Record:
        if names.is_full_sha256(sha):
            return self._images.get(sha, [])
        elif names.is_short_sha256(sha):
            return self._images.get(self._short_sha[sha], [])
        raise ValueError(f"{sha} is not a valid sha256 or short (16 character) sha")

    # Convert to a dictionary that can be written to file as JSON
    # The serialisation and deserialisation are central: able to represent
    # uenv that are available in both JFrog and filesystem directory tree.
    def serialise(self, version: int=UENV_CLI_API_VERSION):
        image_list = []
        for x in self._images.values():
            image_list += x
        terminal.info(f"serialized image list in datastore: {image_list}")
        return {
                "API_VERSION": version,
                "images": [img.dictionary for img in image_list]
        }

    # Convert to a dictionary that can be written to file as JSON
    # The serialisation and deserialisation are central: able to represent
    # uenv that are available in both JFrog and filesystem directory tree.
    @classmethod
    def deserialise(cls, datastore):
        result = cls()
        for img in datastore["images"]:
            result.add_record(Record.from_dictionary(img))

class FileSystemCache():
    def __init__(self, path: str):
        self._path = path
        self._index = path + "/index.json"

        if not os.path.exists(self._index):
            # error: cache does not exists
            raise FileNotFoundError(f"filesystem cache not found {self._path}")

        with open(self._index, "r") as fid:
            raw = json.loads(fid.read())
            self._database = DataStore()
            for img in raw["images"]:
                self._database.add_record(Record.fromjson(img))

    @staticmethod
    def create(path: str, exists_ok: bool=False):
        if not os.path.exists(path):
            terminal.info(f"FileSyStemCache: creating path {path}")
            os.makedirs(path)
        index_file = f"{path}/index.json"
        if not os.path.exists(index_file):
            terminal.info(f"FileSyStemCache: creating empty index {index_file}")
            empty_config = { "API_VERSION": UENV_CLI_API_VERSION, "images": [] }
            with open(index_file, "w") as f:
                # default serialisation is str to serialise the pathlib.PosixPath
                f.write(json.dumps(empty_config, sort_keys=True, indent=2, default=str))
                f.write("\n")

        terminal.info(f"FileSyStemCache: available {index_file}")

    @property
    def database(self):
        return self._database

    def add_record(self, record: Record):
        self._database.add_record(record)

    # The path where an image would be stored
    # will return a path even for images that are not stored
    def image_path(self, r: Record) -> str:
        return self._path + "/images/" + r.sha256

    # Return the full record for a given hash
    # Returns None if no image with that hash is stored in the repo.
    def get_record(self, sha256: str):
        if not names.is_valid_sha(sha256):
            raise ValueError(f"{sha256} is not a valid image sha256 (neither full 64 or short 16 character form)")
        return self._database.get_record(sha256)

    def publish(self):
        with open(self._index, "w") as f:
            # default serialisation is str to serialise the pathlib.PosixPath
            f.write(json.dumps(self._database.serialise(), sort_keys=True, indent=2, default=str))
            f.write("\n")

