Import("env")

# Add gcov library to support coverage instrumentation
env.Append(LIBS=["gcov"])
