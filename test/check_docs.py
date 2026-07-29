import os
import re
import subprocess
import sys
import tempfile

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
COMPILER = sys.argv[1] if len(sys.argv) > 1 else os.path.join(REPO, "build", "b")
DOCS = sys.argv[2:] or [os.path.join(REPO, "README.md")]


def blocks_in(path):
    lines = open(path).read().split("\n")
    found = []
    start = None
    for i, line in enumerate(lines):
        if start is None and line.strip() == "```b":
            start = i
        elif start is not None and line.strip() == "```":
            found.append((start + 2, "\n".join(lines[start + 1:i])))
            start = None
    return found


work = tempfile.mkdtemp()
os.makedirs(os.path.join(work, "std"), exist_ok=True)
for name in os.listdir(os.path.join(REPO, "std")):
    if name.endswith(".b"):
        source = open(os.path.join(REPO, "std", name)).read()
        open(os.path.join(work, "std", name), "w").write(source)

runnable = 0
skipped = 0
failures = []

for doc in DOCS:
    label = os.path.basename(doc)
    for line_no, body in blocks_in(doc):
        if "int main(" not in body or 'import "' in body:
            skipped += 1
            continue
        runnable += 1
        name = "doc_%s_%d.b" % (label.split(".")[0], line_no)
        open(os.path.join(work, name), "w").write(body + "\n")
        result = subprocess.run([COMPILER, name], cwd=work, capture_output=True,
                                text=True, env=dict(os.environ, B_PATH=work))
        if result.returncode != 0:
            output = (result.stdout + result.stderr).strip().split("\n")
            useful = [l for l in output if "error" in l.lower()]
            failures.append((label, line_no, (useful or output[-2:])[:2]))

print("documentation: %d runnable blocks, %d fragments or multi-file excerpts" %
      (runnable, skipped))
for label, line_no, message in failures:
    print("  FAIL %s:%d" % (label, line_no))
    for line in message:
        print("       " + line.strip()[:120])

if failures:
    print("%d documentation block(s) do not compile" % len(failures))
    sys.exit(1)
print("every runnable documentation block compiles")
