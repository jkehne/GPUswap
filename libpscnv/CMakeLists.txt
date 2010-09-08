cmake_minimum_required(VERSION 2.8)

include_directories(../pscnv /usr/include/drm)
add_library(pscnv STATIC libpscnv.c)

