i=0
while (( i++ < 200 )); do
    ln -s $PWD/1.png "$PWD/img/img1-$i.png"
    echo "<img src='img/img1-$i.png' />"
    ln -s $PWD/2.png "$PWD/img/img2-$i.png"
    echo "<img src='img/img2-$i.png' />"
done
