#!/bin/bash

count=$#
if [ "$count" -eq 0 ]; then
    echo "Нет аргументов"
    exit 1
fi

sum=0
for n in "$@"; do
    sum=$((sum + n))
done

echo "Количество: $count"
echo "Среднее арифметическое: $(echo "scale=2; $sum / $count" | bc)"
