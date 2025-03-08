@echo off

tcc webgpu.c -L. -lwgpu_native -shared -rdynamic
tcc main.c -L. -lwebgpu -run