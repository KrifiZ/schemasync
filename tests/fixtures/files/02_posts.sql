CREATE TABLE posts (
    id bigserial PRIMARY KEY,
    author_id int NOT NULL
);

ALTER TABLE posts ADD CONSTRAINT posts_author_id_fkey FOREIGN KEY (author_id) REFERENCES users (id);
