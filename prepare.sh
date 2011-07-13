make mrproper
echo "Old build cleaned"
cp drivers/misc/samsung_modemctl/built-in.443stock_samsung_modemctl drivers/misc/samsung_modemctl/built-in.o
cp drivers/misc/samsung_modemctl/modemctl/built-in.443stock_modemctl drivers/misc/samsung_modemctl/modemctl/built-in.o
echo "Built-in.o modem files copied"

