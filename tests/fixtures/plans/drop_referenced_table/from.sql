CREATE TABLE authors (
    id int PRIMARY KEY,
    name text
);

CREATE TABLE books (
    id int PRIMARY KEY,
    title text,
    author_id int REFERENCES authors
);
