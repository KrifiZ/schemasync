CREATE TABLE posts (
    id bigserial PRIMARY KEY,
    author_id int NOT NULL REFERENCES users (id) ON DELETE CASCADE,
    title text NOT NULL,
    body text,
    published boolean NOT NULL DEFAULT false
);

CREATE INDEX ON posts (author_id);
