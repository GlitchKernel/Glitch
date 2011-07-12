make mrproper
echo "Old build cleaned"
cd drivers/misc/samsung_modemctl && {
cp built-in.443stock_samsung_modemctl built-in.o
cd modemctl && cp built-in.443stock_modemctl built-in.o
}
echo "Built-in.o modem files copied"

