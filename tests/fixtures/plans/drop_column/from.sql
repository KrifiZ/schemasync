CREATE TABLE t (
    id int PRIMARY KEY,
    a text,
    b text
);

CREATE INDEX t_b_idx ON t (b);
