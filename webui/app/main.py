"""Duplex Hunter web UI backend.

Stores uploaded export archives in MongoDB and serves them to the frontend.
An export is the folder the CLI writes to <export-path>/<timestamp>/, zipped
as <timestamp>.zip. The timestamp is the result's unique id and display name.

The JSON files are kept as raw bytes in GridFS: they can exceed the 16 MB
document limit, and their 64-bit hashes do not fit in BSON integers.

Groups of a result can be annotated with a comment and labels. A group is
identified by its section (duplicates or uniques) and its key: the hash, or
the first path for groups that were not hashed.
"""

import io
import json
import os
import re
import zipfile
from datetime import datetime, timezone
from pathlib import Path, PurePosixPath
from typing import Literal, Optional

from contextlib import asynccontextmanager

import gridfs
from bson import ObjectId
from bson.errors import InvalidId
from fastapi import FastAPI, File, HTTPException, Query, UploadFile
from fastapi.middleware.gzip import GZipMiddleware
from fastapi.responses import FileResponse, StreamingResponse
from pydantic import BaseModel, Field
from pymongo import ASCENDING, DESCENDING, MongoClient, ReturnDocument
from pymongo.collation import Collation
from pymongo.errors import DuplicateKeyError

MONGO_URL = os.getenv("MONGO_URL", "mongodb://mongo:27017")
MONGO_DB = os.getenv("MONGO_DB", "duplexhunter")
# Limits the uncompressed size of an archive, as a guard against zip bombs.
MAX_EXTRACTED_MB = int(os.getenv("MAX_EXTRACTED_MB", "2048"))
STATIC_DIR = Path(os.getenv("STATIC_DIR", Path(__file__).parent / "static"))

TIMESTAMP_RE = re.compile(r"^\d{4}-\d{2}-\d{2}_\d{2}-\d{2}-\d{2}$")
KINDS = ("config", "duplicates", "uniques", "analysis")
REQUIRED_KINDS = ("config", "duplicates", "uniques")

Kind = Literal["config", "duplicates", "uniques", "analysis"]
Section = Literal["duplicates", "uniques"]

COLOR_PATTERN = r"^#[0-9a-fA-F]{6}$"
# Label names are unique regardless of case.
LABEL_COLLATION = Collation(locale="en", strength=2)

client = MongoClient(MONGO_URL, serverSelectionTimeoutMS=5000, tz_aware=True)
db = client[MONGO_DB]
results = db["results"]
bucket = gridfs.GridFSBucket(db, bucket_name="exports")
labels = db["labels"]
annotations = db["annotations"]


@asynccontextmanager
async def lifespan(_: FastAPI):
    labels.create_index("name", unique=True, collation=LABEL_COLLATION)
    annotations.create_index(
        [("result", ASCENDING), ("section", ASCENDING), ("key", ASCENDING)], unique=True
    )
    annotations.create_index("labels")
    annotations.create_index([("updatedAt", DESCENDING)])
    yield


app = FastAPI(
    title="Duplex Hunter",
    description="Store and browse Duplex Hunter scan results, and annotate their groups "
    "with comments and labels.",
    version="1.1.0",
    lifespan=lifespan,
)
# Results are large, repetitive JSON, which compresses very well.
app.add_middleware(GZipMiddleware, minimum_size=1024)


class Stats(BaseModel):
    duplicateGroups: int
    duplicateFiles: int
    redundantCopies: int
    uniqueFiles: int
    analyzedGroups: Optional[int] = None


class Result(BaseModel):
    name: str
    uploadedAt: datetime
    sizeBytes: int
    files: list[str]
    config: dict
    stats: Stats
    annotatedGroups: int = 0
    commentedGroups: int = 0


class LabelIn(BaseModel):
    name: str = Field(min_length=1, max_length=40)
    color: str = Field("#6ea8ff", pattern=COLOR_PATTERN)


class LabelUpdate(BaseModel):
    name: Optional[str] = Field(None, min_length=1, max_length=40)
    color: Optional[str] = Field(None, pattern=COLOR_PATTERN)


class Label(LabelIn):
    id: str


class AnnotationIn(BaseModel):
    section: Section
    key: str = Field(min_length=1, max_length=4096, description="Group hash, or first path if not hashed")
    comment: str = Field("", max_length=10000)
    labels: list[str] = Field(default_factory=list, description="Label ids")
    # Kept with the annotation, so annotations can be listed without loading results.
    files: list[str] = Field(default_factory=list)
    size: Optional[int] = None
    hash: Optional[str] = None


class Annotation(AnnotationIn):
    result: str
    updatedAt: Optional[datetime] = None


class Health(BaseModel):
    status: str
    mongo: bool


def to_result(doc: dict, counts: Optional[dict] = None) -> Result:
    return Result(
        name=doc["_id"],
        **{k: v for k, v in doc.items() if k != "_id"},
        **(counts or {}),
    )


def annotation_counts(match: dict) -> dict[str, dict]:
    pipeline = [
        {"$match": match},
        {"$group": {
            "_id": "$result",
            "annotatedGroups": {"$sum": 1},
            "commentedGroups": {"$sum": {"$cond": [{"$ne": ["$comment", ""]}, 1, 0]}},
        }},
    ]
    return {c.pop("_id"): c for c in annotations.aggregate(pipeline)}


def to_label(doc: dict) -> Label:
    return Label(id=str(doc["_id"]), name=doc["name"], color=doc["color"])


def label_oid(label_id: str) -> ObjectId:
    try:
        return ObjectId(label_id)
    except InvalidId:
        raise HTTPException(404, f"Label '{label_id}' not found")


def to_annotation(doc: dict) -> Annotation:
    return Annotation(**{k: v for k, v in doc.items() if k != "_id"})


def find_result(name: str) -> dict:
    doc = results.find_one({"_id": name})
    if doc is None:
        raise HTTPException(404, f"Result '{name}' not found")
    return doc


def extract_exports(archive: zipfile.ZipFile) -> dict[str, bytes]:
    """Returns the export JSON files of an archive, keyed by kind.

    Files are matched by name, so they can be at the root of the archive or
    inside the timestamp folder.
    """
    members: dict[str, zipfile.ZipInfo] = {}
    for info in archive.infolist():
        if info.is_dir():
            continue
        path = PurePosixPath(info.filename)
        if path.parts and path.parts[0] == "__MACOSX":
            continue
        kind = path.stem
        if path.suffix == ".json" and kind in KINDS:
            if kind in members:
                raise HTTPException(400, f"Archive contains more than one {path.name}")
            members[kind] = info

    missing = [k for k in REQUIRED_KINDS if k not in members]
    if missing:
        raise HTTPException(
            400, "Archive is missing " + ", ".join(f"{k}.json" for k in missing)
        )

    limit = MAX_EXTRACTED_MB * 1024 * 1024
    if sum(i.file_size for i in members.values()) > limit:
        raise HTTPException(413, f"Extracted export exceeds {MAX_EXTRACTED_MB} MB")

    data = {}
    for kind, info in members.items():
        with archive.open(info) as f:
            # file_size comes from the archive itself, so it is not trusted.
            raw = f.read(limit + 1)
        if len(raw) > limit:
            raise HTTPException(413, f"Extracted export exceeds {MAX_EXTRACTED_MB} MB")
        data[kind] = raw
    return data


def parse_json(kind: str, raw: bytes):
    try:
        return json.loads(raw)
    except (ValueError, UnicodeDecodeError) as e:
        raise HTTPException(400, f"{kind}.json is not valid JSON: {e}")


def compute_stats(parsed: dict) -> Stats:
    dupes = parsed["duplicates"] or []
    uniques = parsed["uniques"] or []
    analysis = parsed.get("analysis")
    return Stats(
        duplicateGroups=len(dupes),
        duplicateFiles=sum(len(d.get("files", [])) for d in dupes),
        redundantCopies=sum(max(len(d.get("files", [])) - 1, 0) for d in dupes),
        uniqueFiles=len(uniques),
        analyzedGroups=len(analysis) if isinstance(analysis, list) else None,
    )


def delete_files(name: str) -> None:
    for f in bucket.find({"metadata.result": name}):
        bucket.delete(f._id)


@app.get("/api/health", response_model=Health, tags=["system"])
def health():
    """Reports whether the backend can reach MongoDB."""
    try:
        client.admin.command("ping")
        return Health(status="ok", mongo=True)
    except Exception:
        return Health(status="degraded", mongo=False)


@app.get("/api/results", response_model=list[Result], tags=["results"])
def list_results():
    """Lists all stored results, newest first."""
    counts = annotation_counts({})
    return [to_result(d, counts.get(d["_id"])) for d in results.find().sort("_id", DESCENDING)]


@app.post("/api/results", response_model=Result, status_code=201, tags=["results"])
def upload_result(
    file: UploadFile = File(..., description="Export folder zipped as <timestamp>.zip"),
    overwrite: bool = Query(False, description="Replace a result with the same name"),
):
    """Uploads an export archive.

    The archive must be named after the export timestamp
    (e.g. `2026-10-04_13-07-17.zip`) and contain `config.json`,
    `duplicates.json`, `uniques.json` and optionally `analysis.json`.
    """
    filename = PurePosixPath(file.filename or "").name
    if not filename.lower().endswith(".zip"):
        raise HTTPException(400, "File must be a .zip archive")
    name = filename[:-4]
    if not TIMESTAMP_RE.match(name):
        raise HTTPException(
            400, "Archive must be named after the export timestamp, e.g. 2026-10-04_13-07-17.zip"
        )
    if not overwrite and results.count_documents({"_id": name}, limit=1):
        raise HTTPException(409, f"Result '{name}' already exists")

    try:
        with zipfile.ZipFile(file.file) as archive:
            data = extract_exports(archive)
    except zipfile.BadZipFile:
        raise HTTPException(400, "File is not a valid zip archive")

    parsed = {kind: parse_json(kind, raw) for kind, raw in data.items()}
    config = parsed["config"] if isinstance(parsed["config"], dict) else {}

    # Files are replaced before the result document, so a failed upload never
    # leaves a listed result without its files.
    delete_files(name)
    for kind, raw in data.items():
        bucket.upload_from_stream(
            f"{name}/{kind}.json",
            io.BytesIO(raw),
            metadata={"result": name, "kind": kind, "contentType": "application/json"},
        )

    doc = {
        "uploadedAt": datetime.now(timezone.utc),
        "sizeBytes": sum(len(raw) for raw in data.values()),
        "files": [k for k in KINDS if k in data],
        "config": config,
        "stats": compute_stats(parsed).model_dump(),
    }
    results.replace_one({"_id": name}, doc, upsert=True)
    return to_result({"_id": name, **doc})


@app.get("/api/results/{name}", response_model=Result, tags=["results"])
def get_result(name: str):
    """Returns the summary of a stored result."""
    return to_result(find_result(name), annotation_counts({"result": name}).get(name))


@app.get(
    "/api/results/{name}/files/{kind}",
    tags=["results"],
    responses={200: {"content": {"application/json": {}}}},
)
def get_result_file(name: str, kind: Kind):
    """Returns one JSON file of a result exactly as the CLI exported it."""
    find_result(name)
    try:
        stream = bucket.open_download_stream_by_name(f"{name}/{kind}.json")
    except gridfs.errors.NoFile:
        raise HTTPException(404, f"Result '{name}' has no {kind}.json")
    return StreamingResponse(
        iter(lambda: stream.readchunk(), b""),
        media_type="application/json",
        headers={"Content-Length": str(stream.length)},
    )


@app.get(
    "/api/results/{name}/download",
    tags=["results"],
    responses={200: {"content": {"application/zip": {}}}},
)
def download_result(name: str):
    """Downloads a result as a zip archive, in the same layout as the upload."""
    doc = find_result(name)
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as archive:
        for kind in doc["files"]:
            raw = bucket.open_download_stream_by_name(f"{name}/{kind}.json").read()
            archive.writestr(f"{name}/{kind}.json", raw)
    buf.seek(0)
    return StreamingResponse(
        buf,
        media_type="application/zip",
        headers={"Content-Disposition": f'attachment; filename="{name}.zip"'},
    )


@app.delete("/api/results/{name}", status_code=204, tags=["results"])
def delete_result(name: str):
    """Deletes a result, its files and its annotations."""
    find_result(name)
    results.delete_one({"_id": name})
    delete_files(name)
    annotations.delete_many({"result": name})


@app.get("/api/results/{name}/annotations", response_model=list[Annotation], tags=["annotations"])
def list_result_annotations(name: str):
    """Lists the annotated groups of a result."""
    find_result(name)
    return [to_annotation(d) for d in annotations.find({"result": name})]


@app.put("/api/results/{name}/annotations", response_model=Annotation, tags=["annotations"])
def set_annotation(name: str, body: AnnotationIn):
    """Sets the comment and labels of a group, replacing the previous ones.

    A group without a comment and labels is not stored, so sending both empty
    removes its annotation.
    """
    find_result(name)
    body.comment = body.comment.strip()
    body.labels = list(dict.fromkeys(body.labels))

    ids = [ObjectId(i) for i in body.labels if ObjectId.is_valid(i)]
    known = {str(d["_id"]) for d in labels.find({"_id": {"$in": ids}})}
    unknown = [i for i in body.labels if i not in known]
    if unknown:
        raise HTTPException(400, "Unknown label ids: " + ", ".join(unknown))

    where = {"result": name, "section": body.section, "key": body.key}
    if not body.comment and not body.labels:
        annotations.delete_one(where)
        return Annotation(result=name, **body.model_dump())

    doc = annotations.find_one_and_update(
        where,
        {"$set": {**body.model_dump(), "result": name, "updatedAt": datetime.now(timezone.utc)}},
        upsert=True,
        return_document=ReturnDocument.AFTER,
    )
    return to_annotation(doc)


@app.get("/api/annotations", response_model=list[Annotation], tags=["annotations"])
def search_annotations(
    label: Optional[str] = Query(None, description="Only groups with this label id"),
    commented: Optional[bool] = Query(None, description="Only groups with (true) or without (false) a comment"),
    result: Optional[str] = Query(None, description="Only groups of this result"),
):
    """Lists annotated groups of all results, most recently changed first."""
    query: dict = {}
    if label is not None:
        query["labels"] = label
    if commented is not None:
        query["comment"] = {"$ne": ""} if commented else ""
    if result is not None:
        query["result"] = result
    return [to_annotation(d) for d in annotations.find(query).sort("updatedAt", DESCENDING)]


@app.get("/api/labels", response_model=list[Label], tags=["labels"])
def list_labels():
    """Lists all labels, by name."""
    return [to_label(d) for d in labels.find().sort("name", ASCENDING).collation(LABEL_COLLATION)]


@app.post("/api/labels", response_model=Label, status_code=201, tags=["labels"])
def create_label(body: LabelIn):
    """Adds a label, which can then be assigned to groups."""
    doc = {"name": body.name.strip(), "color": body.color}
    try:
        doc["_id"] = labels.insert_one(doc).inserted_id
    except DuplicateKeyError:
        raise HTTPException(409, f"Label '{doc['name']}' already exists")
    return to_label(doc)


@app.patch("/api/labels/{label_id}", response_model=Label, tags=["labels"])
def update_label(label_id: str, body: LabelUpdate):
    """Renames or recolors a label."""
    changes = body.model_dump(exclude_none=True)
    if "name" in changes:
        changes["name"] = changes["name"].strip()
    try:
        doc = labels.find_one_and_update(
            {"_id": label_oid(label_id)}, {"$set": changes}, return_document=ReturnDocument.AFTER
        ) if changes else labels.find_one({"_id": label_oid(label_id)})
    except DuplicateKeyError:
        raise HTTPException(409, f"Label '{changes['name']}' already exists")
    if doc is None:
        raise HTTPException(404, f"Label '{label_id}' not found")
    return to_label(doc)


@app.delete("/api/labels/{label_id}", status_code=204, tags=["labels"])
def delete_label(label_id: str):
    """Deletes a label and removes it from all groups."""
    if labels.delete_one({"_id": label_oid(label_id)}).deleted_count == 0:
        raise HTTPException(404, f"Label '{label_id}' not found")
    annotations.update_many({"labels": label_id}, {"$pull": {"labels": label_id}})
    annotations.delete_many({"labels": {"$size": 0}, "comment": ""})


@app.get("/", include_in_schema=False)
def index():
    return FileResponse(STATIC_DIR / "index.html")
