CREATE TABLE t (
    id int PRIMARY KEY,
    a int,
    b int,
    c int,
    UNIQUE (b)
);

CREATE INDEX t_a_idx ON t (a);
