CREATE TABLE a (
    id int PRIMARY KEY,
    b_id int
);

CREATE TABLE b (
    id int PRIMARY KEY,
    a_id int REFERENCES a
);

ALTER TABLE a ADD CONSTRAINT a_b_id_fkey FOREIGN KEY (b_id) REFERENCES b (id);

CREATE TABLE employees (
    id int PRIMARY KEY,
    manager_id int REFERENCES employees (id) ON DELETE SET NULL
);
