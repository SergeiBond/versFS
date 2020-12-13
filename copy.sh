echo "Enter path to storage. E.g., /home/sbondarenko21/sysproj-8/stg/"
read stgpath

echo "Enter path to file you want to dump. E.g., 1.txt"
read srcpath

echo "Enter path to the destination folder you want to dump to."
read dstpath
if [ ! -d "$stgpath" ]; then
    echo "Source path: $stgpath doesn't exist"
    exit 1
fi
    


mkdir -p "$dstpath"
cp "$stgpath/$srcpath"* "$dstpath"

