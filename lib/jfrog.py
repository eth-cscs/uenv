import requests

from datastore import DataStore
from record import Record
import terminal


# The https://cicd-ext-mw.cscs.ch/uenv/list API endpoint returns
# a list of images in the jfrog uenv.
#
#{
#  "results":
#  [
#    {
#      "repo" : "uenv",
#      "path" : "build/clariden/zen3/prgenv-gnu/23.11/1094139948",
#      "name" : "manifest.json",
#      "created" : "2023-12-04T09:05:44.034Z",
#      "size" : "123683707",
#      "sha256" : "134c04d01bb3583726804a094b144d3637997877ef6162d1fe19eabff3c72c3a",
#      "stats" : [{
#        "downloaded" : "2023-12-11T17:56:59.052Z",
#        "downloads" : 11
#      }]
#    },
#    ...
#  ],
#  "range" :
#  {
#    "start_pos" : 0,
#    "end_pos" : 22,
#    "total" : 22
#  }
#}
#

def query() -> tuple:
    try:
        # GET request to the middleware
        url = "https://cicd-ext-mw.cscs.ch/uenv/list"
        terminal.info(f"querying jfrog at {url}")
        response = requests.get(url)
        response.raise_for_status()

        raw_records = response.json()

        deploy_database = DataStore()
        build_database = DataStore()

        for record in raw_records["results"]:
            path = record["path"]

            date = Record.to_datetime(record["created"])
            sha256 = record["sha256"]
            size = record["size"]
            if path.startswith("build/"):
                r = Record.frompath(path[len("build/"):], date, size, sha256)
                build_database.add_record(r)
            if path.startswith("deploy/"):
                r = Record.frompath(path[len("deploy/"):], date, size, sha256)
                deploy_database.add_record(r)

        return (deploy_database, build_database)

    except Exception as error:
        raise RuntimeError(f"downloading image data from jfrog.svs.cscs.ch ({str(error)})")

def relative_from_record(record):
    return f"{record.system}/{record.uarch}/{record.name}/{record.version}:{record.tag}"

def address(record: Record, namespace: str):
    if namespace!='deploy' and namespace!='build':
        raise RuntimeError("namespace must be one of build or deploy")
    return f"jfrog.svc.cscs.ch/uenv/{namespace}/{relative_from_record(record)}"
