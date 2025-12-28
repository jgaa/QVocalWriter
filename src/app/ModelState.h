#pragma once

enum class ModelState {
    CREATED,
    RUNNING,
    PREPARING,
    LOADING,
    LOADED,
    READY,
    WORKING,
    STOPPING,
    DONE,
    ERROR
};
