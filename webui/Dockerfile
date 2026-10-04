FROM python:3.12-slim

ENV PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1

WORKDIR /srv

COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

COPY app/ app/
# The frontend is the same page as docs/duplex-hunter-ui.html, passed in
# through the "docs" build context in docker-compose.yml.
COPY --from=docs duplex-hunter-ui.html app/static/index.html

RUN useradd --system --no-create-home app
USER app

EXPOSE 8080

HEALTHCHECK --interval=30s --timeout=5s --start-period=10s \
    CMD python -c "import urllib.request; urllib.request.urlopen('http://localhost:8080/api/health')"

CMD ["uvicorn", "app.main:app", "--host", "0.0.0.0", "--port", "8080"]
