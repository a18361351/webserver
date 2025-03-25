# 编译器设置
CXX := g++
CXXFLAGS := -Iinc -Wall -Wextra -g -MMD -pthread
LDFLAGS := -pthread

# 文件路径设置
SRC_DIR := src
OBJ_DIR := obj
BIN_DIR := bin
TARGET := server
TARGET_PATH := $(BIN_DIR)/$(TARGET)  # 完整路径

# 源文件列表（明确指定）
SRCS := \
	$(SRC_DIR)/http_conn.cpp \
	$(SRC_DIR)/parser.cpp \
	$(SRC_DIR)/server.cpp

# 生成对应的目标文件列表
OBJS := $(patsubst $(SRC_DIR)/%.cpp, $(OBJ_DIR)/%.o, $(SRCS))
DEP_FILES := $(patsubst $(OBJ_DIR)/%.o, $(OBJ_DIR)/%.d, $(OBJS))

# 默认目标
all: $(TARGET_PATH)

# 主目标链接规则
$(TARGET_PATH): $(OBJS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

# 编译源文件到目标文件
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# 包含自动生成的依赖关系
-include $(DEP_FILES)

# 清理命令
.PHONY: clean
clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)

# 清理命令
.PHONY: clean