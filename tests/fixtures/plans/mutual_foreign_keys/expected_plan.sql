CREATE TABLE "public"."a" (
    "id" int4 NOT NULL,
    "b_id" int4,
    CONSTRAINT "a_pkey" PRIMARY KEY ("id")
);

CREATE TABLE "public"."b" (
    "id" int4 NOT NULL,
    "a_id" int4,
    CONSTRAINT "b_pkey" PRIMARY KEY ("id")
);

CREATE TABLE "public"."employees" (
    "id" int4 NOT NULL,
    "manager_id" int4,
    CONSTRAINT "employees_pkey" PRIMARY KEY ("id")
);

ALTER TABLE "public"."a" ADD CONSTRAINT "a_b_id_fkey" FOREIGN KEY ("b_id") REFERENCES "public"."b" ("id");

ALTER TABLE "public"."b" ADD CONSTRAINT "b_a_id_fkey" FOREIGN KEY ("a_id") REFERENCES "public"."a" ("id");

ALTER TABLE "public"."employees" ADD CONSTRAINT "employees_manager_id_fkey" FOREIGN KEY ("manager_id") REFERENCES "public"."employees" ("id") ON DELETE SET NULL;

