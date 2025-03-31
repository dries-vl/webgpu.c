@echo off

tcc main.c webgpu.c -L. -I. -run -lwgpu_native