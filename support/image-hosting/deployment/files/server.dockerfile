FROM python:3.10

RUN apt-get update && apt-get install -y firefox-esr

WORKDIR /app

RUN pip install poetry

COPY pyproject.toml poetry.lock /app/
COPY ./src /app/src

RUN poetry config virtualenvs.create false \
  && poetry install --no-interaction --no-ansi

ENV BASE_URL=""
ENV SCREENSHOT_URL=""
ENV ACCESS_TOKEN=""

EXPOSE 8765

CMD poetry run hei-server --base-url="${BASE_URL}" --screenshot-url="${SCREENSHOT_URL}" --access-token="${ACCESS_TOKEN}" --capture-interval=10

