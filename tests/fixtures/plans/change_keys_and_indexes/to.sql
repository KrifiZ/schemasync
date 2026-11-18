CREATE TABLE t (
    id int,
    a int,
    b int,
    c int,
    PRIMARY KEY (id, a),
    UNIQUE (b, c)
);

CREATE INDEX t_a_idx ON t (a, b);
CREATE UNIQUE INDEX t_c_idx ON t (c);
